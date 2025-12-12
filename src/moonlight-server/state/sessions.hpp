#pragma once

#include <events/events.hpp>
#include <helpers/logger.hpp>
#include <helpers/utils.hpp>
#include <immer/vector.hpp>
#include <optional>
#include <range/v3/view.hpp>
#include <state/config.hpp>
#include <state/serialised_config.hpp>

namespace state {

using namespace wolf::core;

inline std::optional<events::StreamSession> get_session_by_id(const immer::vector<events::StreamSession> &sessions,
                                                              const std::size_t id) {
  auto results =
      sessions |                                                                                             //
      ranges::views::filter([id](const events::StreamSession &session) { return session.session_id == id; }) //
      | ranges::views::take(1)                                                                               //
      | ranges::to_vector;                                                                                   //
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple sessions for a given ID: {}", id);
    return {};
  }
}

/**
 * 通过 rtsp_fake_ip 查找 session
 * 由于现在 session_id 是基于 rtsp_fake_ip 的，这是查找特定 session 的推荐方式
 */
inline std::optional<events::StreamSession> get_session_by_rtsp_fake_ip(const immer::vector<events::StreamSession> &sessions,
                                                                        const std::string &rtsp_fake_ip) {
  auto results = sessions |
                 ranges::views::filter([rtsp_fake_ip](const events::StreamSession &session) {
                   return session.rtsp_fake_ip == rtsp_fake_ip;
                 }) |
                 ranges::views::take(1) |
                 ranges::to_vector;
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple sessions for rtsp_fake_ip: {}", rtsp_fake_ip);
    return {};
  }
}

/**
 * 通过客户端查找 session
 * 注意：由于现在 session_id 是基于 rtsp_fake_ip 的，无法从 session_id 反推 client_id
 * 此函数通过遍历所有 session 并重新计算 session_id 来查找匹配的 session
 * 如果客户端有多个 session，此函数返回第一个匹配的
 * 建议使用 get_session_by_rtsp_fake_ip 来精确查找特定 session
 */
inline std::optional<events::StreamSession> get_session_by_client(const immer::vector<events::StreamSession> &sessions,
                                                                  const wolf::config::PairedClient &client) {
  auto client_id = get_client_id(client);
  // 遍历所有 session，重新计算 session_id 来查找匹配的
  for (const events::StreamSession &session : sessions) {
    // 重新计算 session_id：client_id@rtsp_fake_ip 的哈希
    auto expected_session_id_str = fmt::format("{}@{}", client_id, session.rtsp_fake_ip);
    auto expected_session_id = std::hash<std::string>{}(expected_session_id_str);
    if (session.session_id == expected_session_id) {
      return session;
    }
  }
  return {};
}

/**
 * 通过客户端查找所有 sessions
 * 返回该客户端下的所有 sessions（可能有多个）
 * 注意：由于现在 session_id 是基于 rtsp_fake_ip 的，无法从 session_id 反推 client_id
 * 此函数通过遍历所有 session 并重新计算 session_id 来查找所有匹配的 sessions
 */
inline immer::vector<events::StreamSession> get_sessions_by_client(const immer::vector<events::StreamSession> &sessions,
                                                                   const wolf::config::PairedClient &client) {
  auto client_id = get_client_id(client);
  // 遍历所有 session，重新计算 session_id 来查找所有匹配的
  auto results = sessions |
                 ranges::views::filter([client_id](const events::StreamSession &session) {
                   // 重新计算 session_id：client_id@rtsp_fake_ip 的哈希
                   auto expected_session_id_str = fmt::format("{}@{}", client_id, session.rtsp_fake_ip);
                   auto expected_session_id = std::hash<std::string>{}(expected_session_id_str);
                   return session.session_id == expected_session_id;
                 }) |
                 ranges::to<immer::vector<events::StreamSession>>();
  return results;
}

inline std::optional<events::Lobby> get_lobby_by_id(const immer::vector<events::Lobby> &lobbies,
                                                    std::string_view lobby_id) {
  auto results = lobbies |                                                                                      //
                 ranges::views::filter([lobby_id](const events::Lobby &lobby) { return lobby.id == lobby_id; }) //
                 | ranges::views::take(1)                                                                       //
                 | ranges::to_vector;                                                                           //
  if (results.size() == 1) {
    return results[0];
  } else if (results.empty()) {
    return {};
  } else {
    logs::log(logs::warning, "Found multiple lobbies for a given ID: {}", lobby_id);
    return {};
  }
}

inline std::optional<events::Lobby> get_lobby_by_connected_session(const immer::vector<events::Lobby> &lobbies,
                                                                   std::string_view session_id) {
  for (const events::Lobby &lobby : lobbies) {
    immer::vector<immer::box<std::string>> sessions = lobby.connected_sessions->load();
    auto session = std::find_if(sessions.begin(), sessions.end(), [session_id](const auto &session) {
      return session == session_id;
    });
    if (session == sessions.end()) {
      continue;
    }
    return lobby;
  }
  return {};
}

inline std::shared_ptr<events::StreamSession> create_stream_session(immer::box<state::AppState> state,
                                                                    const events::App &run_app,
                                                                    const wolf::config::PairedClient &current_client,
                                                                    const moonlight::DisplayMode &display_mode,
                                                                    int audio_channel_count,
                                                                    const std::string &aes_key,
                                                                    const std::string &aes_iv) {
  auto full_path = std::filesystem::path(state->host->local_base_state_folder) / current_client.app_state_folder /
                   run_app.base.title;
  logs::log(logs::debug, "Host app state folder: {}, creating paths", full_path.string());
  std::filesystem::create_directories(full_path);

  std::random_device rd;
  std::mt19937 generator(rd());

  std::uniform_int_distribution<> chars(33, 126); // ASCII values for printable character
  std::array<char, 16> rtp_secret_payload;
  for (auto &c : rtp_secret_payload) {
    c = static_cast<char>(chars(generator));
  }

  std::uniform_int_distribution<u_int32_t> uints(0, UINT32_MAX);

  std::uniform_int_distribution<> ints(0, 255);
  auto rtsp_fake_ip = fmt::format("{}.{}.{}.{}", ints(generator), ints(generator), ints(generator), ints(generator));

  // 使用 rtsp_fake_ip 来构成 session_id，用于区分多个 session
  // 将 client_id 和 rtsp_fake_ip 组合后哈希，确保每个 session 都有唯一的 ID
  auto client_id = get_client_id(current_client);
  auto session_id_str = fmt::format("{}@{}", client_id, rtsp_fake_ip);
  auto session_id = std::hash<std::string>{}(session_id_str);

  auto session = events::StreamSession{
      .display_mode = display_mode,
      .audio_channel_count = audio_channel_count,
      .event_bus = state->event_bus,
      .client_settings = current_client.settings,
      .app = std::make_shared<events::App>(run_app),
      .app_local_state_folder = full_path.string(),
      .app_host_state_folder = std::filesystem::path(state->host->host_base_state_folder) /
                               current_client.app_state_folder / run_app.base.title,

      .aes_key = aes_key,
      .aes_iv = aes_iv,

      // Moonlight protocol extension to support IP-less connections
      .rtp_secret_payload = rtp_secret_payload,
      .enet_secret_payload = uints(generator),
      .rtsp_fake_ip = rtsp_fake_ip,

      // client info
      // 使用 rtsp_fake_ip 构成的 session_id，每个 session 都有唯一的 ID
      .session_id = session_id,
      .video_stream_port = static_cast<unsigned short>(get_port(VIDEO_PING_PORT)),
      .audio_stream_port = static_cast<unsigned short>(get_port(AUDIO_PING_PORT)),
      .control_stream_port = static_cast<unsigned short>(get_port(CONTROL_PORT))};

  return std::make_shared<events::StreamSession>(session);
}

inline immer::vector<events::StreamSession> remove_session(const immer::vector<events::StreamSession> &sessions,
                                                           const events::StreamSession &session) {
  return sessions                                                                                           //
         | ranges::views::filter([remove_hash = session.session_id](const events::StreamSession &cur_ses) { //
             return cur_ses.session_id != remove_hash;                                                      //
           })                                                                                               //
         | ranges::to<immer::vector<events::StreamSession>>();                                              //
}
} // namespace state
