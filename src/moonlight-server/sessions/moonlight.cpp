#include <immer/array_transient.hpp>
#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>
#include <sessions/handlers.hpp>
#include <state/sessions.hpp>
#include <streaming/streaming.hpp>

namespace wolf::core::sessions {

using session_devices = immer::map<std::string /* session_id */, std::shared_ptr<events::devices_atom_queue>>;

/**
 * Will stop the execution until an event of type RTPPingType is triggered
 * and the signature is matching the input `sess`.
 * Returns the RTPPingType event
 */
template <typename RTPPingType>
immer::box<RTPPingType> wait_for_ping(std::shared_ptr<events::EventBusType> ev_bus, const auto &sess) {
  auto ping_promise = std::make_shared<std::promise<RTPPingType>>();
  auto ping_future = ping_promise->get_future();

  auto handler =
      ev_bus->register_handler<immer::box<RTPPingType>>([sess, ping_promise](const immer::box<RTPPingType> &ping_ev) {
        // Check if this ping is for our session
        if (sess->rtp_secret_payload == ping_ev->payload || // Secret payload matching
            (!ping_ev->payload.has_value() && ping_ev->client_ip == sess->client_ip &&
             ping_ev->client_port == sess->port)) { // Legacy IP+port matching when no payload has been passed
          // Resolve the promise with the ping event data
          ping_promise->set_value(*ping_ev);
        }
      });

  // Wait for the promise to be fulfilled
  auto ping_ev = ping_future.get();

  // Unregister the handler since we only need it once
  handler.unregister();

  return ping_ev;
}

immer::vector<immer::box<events::EventBusHandlers>>
setup_moonlight_handlers(const immer::box<state::AppState> &app_state,
                         const std::string &runtime_dir,
                         const std::optional<AudioServer> &audio_server) {
  immer::vector_transient<immer::box<events::EventBusHandlers>> handlers;

  /*
   * A queue of devices that are waiting to be plugged, mapped by session_id
   * This way we can accumulate devices here until the docker container is up and running
   */
  auto plugged_devices_queue = std::make_shared<immer::atom<session_devices>>();

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [&app_state, plugged_devices_queue](const immer::box<events::StopStreamEvent> &ev) {
        // Remove session from app state so that HTTP/S applist gets updated
        // This should effectively destroy the virtual Wayland session since it holds the last reference
        app_state->running_sessions->update([&ev](const immer::vector<events::StreamSession> &ses_v) {
          return state::remove_session(ses_v, {.session_id = ev->session_id});
        });

        plugged_devices_queue->update([=](const auto map) { return map.erase(std::to_string(ev->session_id)); });
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::PlugDeviceEvent>>(
      [plugged_devices_queue, lobbies = app_state->lobbies](const immer::box<events::PlugDeviceEvent> &hotplug_ev) {
        logs::log(logs::debug, "{} received hot-plug device event", hotplug_ev->session_id);

        // If we are currently in a lobby we don't want to plug the device to the original wolf-ui session
        if (!state::get_lobby_by_connected_session(lobbies->load(), hotplug_ev->session_id)) {
          if (auto session_devices_queue = plugged_devices_queue->load()->find(hotplug_ev->session_id)) {
            session_devices_queue->get()->push(hotplug_ev);
          } else {
            logs::log(logs::warning, "Unable to find plugged_devices_queue for session {}", hotplug_ev->session_id);
          }
        } else {
          // This event will be picked up by the lobbies handler
          logs::log(logs::debug, "Session {} is in a lobby, ignoring hot-plug device event", hotplug_ev->session_id);
        }
      }));

  // Run process and our custom wayland as soon as a new StreamSession is created
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StreamSession>>(
      [=](const immer::box<events::StreamSession> &session) {
        /* Initialise plugged device queue */
        auto devices_q = std::make_shared<events::devices_atom_queue>();
        plugged_devices_queue->update(
            [=](const session_devices map) { return map.set(std::to_string(session->session_id), devices_q); });

        // 检查 session 是否在 lobby 中，如果在就不创建 producer pipeline（使用 lobby 的 producer）
        auto lobbies = app_state->lobbies->load();
        auto lobby = state::get_lobby_by_connected_session(lobbies.get(), std::to_string(session->session_id));
        bool is_in_lobby = lobby.has_value();

        std::shared_ptr<boost::promise<streaming::WaylandDisplayReady>> on_ready =
            std::make_shared<boost::promise<streaming::WaylandDisplayReady>>();

        if (session->app->start_virtual_compositor && !is_in_lobby) {
          logs::log(logs::debug, "[STREAM_SESSION] Create wayland compositor");

          // Start Gstreamer producer pipeline
          std::thread([session, on_ready, gst_context = app_state->gst_context]() {
            streaming::start_video_producer(std::to_string(session->session_id),
                                            session->app->video_producer_buffer_caps,
                                            session->app->render_node,
                                            {.width = session->display_mode.width,
                                             .height = session->display_mode.height,
                                             .refreshRate = session->display_mode.refreshRate},
                                            gst_context,
                                            on_ready,
                                            session->event_bus);
          }).detach();
        } else if (is_in_lobby) {
          logs::log(logs::debug, "[STREAM_SESSION] Session {} is in lobby {}, skipping producer pipeline creation",
                    session->session_id,
                    lobby->id);
          // Session 在 lobby 中，使用 lobby 的 wayland display
          auto wl_state_ptr = lobby->wayland_display->load();
          if (wl_state_ptr.get() != nullptr) {
            session->wayland_display->store(wl_state_ptr);
            // 从 WaylandState 获取 socket name
            auto socket_name = virtual_display::get_wayland_socket_name(*(wl_state_ptr.get()));
            // wayland_plugin 在 WaylandState 中不可直接访问，但后续代码会从 session->wayland_display 获取
            // 这里先设置为空，后续代码会正确处理
            on_ready->set_value({.wayland_socket_name = socket_name, .wayland_plugin = nullptr});
          } else {
            // Lobby 的 wayland display 还没准备好，等待
            logs::log(logs::warning,
                      "[STREAM_SESSION] Lobby {} wayland display not ready yet, session may not work correctly",
                      lobby->id);
            on_ready->set_value({});
          }
        } else {
          // Create virtual devices
          auto mouse = input::Mouse::create();
          if (!mouse) {
            logs::log(logs::error, "Failed to create mouse: {}", mouse.getErrorMessage());
          } else {
            auto mouse_ptr = input::Mouse(std::move(*mouse));
            devices_q->push(immer::box<events::PlugDeviceEvent>(
                events::PlugDeviceEvent{.session_id = std::to_string(session->session_id),
                                        .udev_events = mouse_ptr.get_udev_events(),
                                        .udev_hw_db_entries = mouse_ptr.get_udev_hw_db_entries()}));
            session->mouse->emplace(std::move(mouse_ptr));
          }

          auto keyboard = input::Keyboard::create();
          if (!keyboard) {
            logs::log(logs::error, "Failed to create keyboard: {}", keyboard.getErrorMessage());
          } else {
            auto keyboard_ptr = input::Keyboard(std::move(*keyboard));
            devices_q->push(immer::box<events::PlugDeviceEvent>(
                events::PlugDeviceEvent{.session_id = std::to_string(session->session_id),
                                        .udev_events = keyboard_ptr.get_udev_events(),
                                        .udev_hw_db_entries = keyboard_ptr.get_udev_hw_db_entries()}));
            session->keyboard->emplace(std::move(keyboard_ptr));
          }
          on_ready->set_value({});
        }

        /* Create audio virtual sink */
        // 如果 session 在 lobby 中，使用 lobby 的 audio sink，不创建自己的
        if (!is_in_lobby) {
          logs::log(logs::debug, "[STREAM_SESSION] Create virtual audio sink");
          auto pulse_sink_name = fmt::format("virtual_sink_{}", session->session_id);
          std::shared_ptr<audio::VSink> v_device;
          if (session->app->start_audio_server && audio_server && audio_server->server) {
            v_device = audio::create_virtual_sink(
                audio_server->server,
                audio::AudioDevice{.sink_name = pulse_sink_name,
                                   .mode = state::get_audio_mode(session->audio_channel_count, true)});
            session->audio_sink->store(v_device);

            std::thread([session, audio_server = audio_server->server]() {
              auto sink_name = fmt::format("virtual_sink_{}.monitor", session->session_id);
              streaming::start_audio_producer(std::to_string(session->session_id),
                                              session->event_bus,
                                              session->audio_channel_count,
                                              sink_name,
                                              audio::get_server_name(audio_server));
            }).detach();
          }
        } else {
          logs::log(logs::debug,
                    "[STREAM_SESSION] Session {} is in lobby {}, using lobby's audio sink",
                    session->session_id,
                    lobby->id);
          // 使用 lobby 的 audio sink
          auto lobby_audio_sink = lobby->audio_sink->load();
          if (lobby_audio_sink.get() != nullptr) {
            session->audio_sink->store(lobby_audio_sink);
          }
        }

        // TODO: timeout? What if the wayland display is never ready?
        auto w_display_ready = on_ready->get_future().then([session](auto fut) {
          streaming::WaylandDisplayReady ready = fut.get();

          // 如果 wayland_plugin 为空，说明 session 在 lobby 中，wayland_display 已经设置好了
          if (ready.wayland_plugin == nullptr) {
            // Session 在 lobby 中，使用已设置的 wayland_display
            // 注意：虚拟设备应该已经由 JoinLobbyEvent handler 设置好了
            // 这里只检查一下，如果还没有设置，就设置一下（防止时序问题）
            auto wl_state = session->wayland_display->load();
            if (wl_state.get() != nullptr) {
              // 检查虚拟设备是否已经设置（JoinLobbyEvent handler 可能已经设置了）
              if (!session->mouse->has_value()) {
                session->mouse->emplace(virtual_display::WaylandMouse(wl_state));
              }
              if (!session->keyboard->has_value()) {
                session->keyboard->emplace(virtual_display::WaylandKeyboard(wl_state));
              }
              if (!session->touch_screen->has_value()) {
                session->touch_screen->emplace(virtual_display::WaylandTouchScreen(wl_state));
              }
            }
            // Session 在 lobby 中，不需要启动自己的 runner（lobby 已经有 runner 了）
            logs::log(logs::debug, "[STREAM_SESSION] Session {} is in lobby, skipping runner start", session->session_id);
          } else {
            // 正常流程：创建新的 wayland display
            auto wl_state = virtual_display::create_wayland_display(ready.wayland_plugin, ready.wayland_socket_name);
            // Set the wayland display
            session->wayland_display->store(wl_state);

            // Set virtual devices
            session->mouse->emplace(virtual_display::WaylandMouse(wl_state));
            session->keyboard->emplace(virtual_display::WaylandKeyboard(wl_state));
            session->touch_screen->emplace(virtual_display::WaylandTouchScreen(wl_state));

            // 启动 session 自己的 runner
            logs::log(logs::debug, "[STREAM_SESSION] Start runner");
            session->event_bus->fire_event(immer::box<events::StartRunner>(
                events::StartRunner{.stop_stream_when_over = true,
                                    .runner = session->app->runner,
                                    .stream_session = std::make_shared<events::StreamSession>(*session)}));
          }
        });
      }));

  /* Start runner */
  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::StartRunner>>(
      [=](const immer::box<events::StartRunner> &run_session) {
        auto session_id = std::to_string(run_session->stream_session->session_id);
        auto devices_q = plugged_devices_queue->load()->find(session_id);
        if (!devices_q) {
          logs::log(logs::warning, "No devices queue found for session {}", session_id);
          return;
        }

        std::thread([=]() {
          start_runner(
              run_session->runner,
              *devices_q,
              immer::box<RunnerArgs>{RunnerArgs{
                  .session_id = session_id,
                  .video_settings =
                      {
                          .width = run_session->stream_session->display_mode.width,
                          .height = run_session->stream_session->display_mode.height,
                          .refresh_rate = run_session->stream_session->display_mode.refreshRate,
                          .wayland_render_node = run_session->stream_session->app->render_node,
                          .runner_render_node = run_session->stream_session->app->render_node,
                          .video_producer_buffer_caps = run_session->stream_session->app->video_producer_buffer_caps,
                      },
                  .wayland_display = run_session->stream_session->wayland_display->load(),
                  .audio_server = audio_server,
                  .audio_sink = run_session->stream_session->audio_sink->load(),
                  .host = app_state->host,
                  .app_local_state_folder = run_session->stream_session->app_local_state_folder,
                  .app_host_state_folder = run_session->stream_session->app_host_state_folder,
                  .xdg_runtime_dir = runtime_dir,
                  .client_settings = run_session->stream_session->client_settings}});

          // Runner process ended
          if (run_session->stop_stream_when_over) {
            run_session->stream_session->wayland_display->store(nullptr);

            app_state->event_bus->fire_event(immer::box<events::StopStreamEvent>(
                events::StopStreamEvent{.session_id = run_session->stream_session->session_id}));
          }
        }).detach();
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::VideoSession>>(
      [ev_bus = app_state->event_bus,
       gst_context = app_state->gst_context](const immer::box<events::VideoSession> &sess) {
        // Start a thread that will wait for the RTP ping event
        std::thread([sess, ev_bus, gst_context]() {
          auto ping_ev = wait_for_ping<events::RTPVideoPingEvent>(ev_bus, sess);

          // Start streaming
          streaming::start_streaming_video(sess,
                                           ev_bus,
                                           ping_ev->client_ip,
                                           ping_ev->client_port,
                                           gst_context,
                                           ping_ev->video_socket.get());
        }).detach();
      }));

  handlers.push_back(app_state->event_bus->register_handler<immer::box<events::AudioSession>>(
      [ev_bus = app_state->event_bus, audio_server](const immer::box<events::AudioSession> &sess) {
        // Start a thread that will wait for the RTP ping event
        std::thread([sess, ev_bus, audio_server]() {
          auto ping_ev = wait_for_ping<events::RTPAudioPingEvent>(ev_bus, sess);

          // Start streaming
          auto audio_server_name = audio_server ? audio::get_server_name(audio_server->server)
                                                : std::optional<std::string>();
          auto sink_name = fmt::format("virtual_sink_{}.monitor", sess->session_id);
          auto server_name = audio_server_name ? audio_server_name.value() : "";

          streaming::start_streaming_audio(sess,
                                           ev_bus,
                                           ping_ev->client_ip,
                                           ping_ev->client_port,
                                           ping_ev->audio_socket.get(),
                                           sink_name,
                                           server_name);
        }).detach();
      }));

  return handlers.persistent();
}

} // namespace wolf::core::sessions