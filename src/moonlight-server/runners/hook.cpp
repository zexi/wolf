#include "boost/json/value_from.hpp"
#include <boost/log/trivial.hpp>
#include <core/docker.hpp>
#include <curl/curl.h>
#include <helpers/logger.hpp>
#include <runners/hook.hpp>
#include <string_view>

namespace wolf::core::hook {

using namespace std::chrono_literals;
using namespace ranges::views;
using namespace wolf::core;
namespace json = boost::json;

using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

std::optional<std::pair<long, std::string>> request(docker::METHOD method,
                                                    std::string_view target,
                                                    bool debug = false,
                                                    std::string_view post_body = {},

                                                    const std::vector<std::string> &header_params = {}) {
  auto curl = curl_easy_init();
  if (debug) {
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  }
  auto curl_p = curl_ptr(curl, ::curl_easy_cleanup);
  return docker::req(curl_p.get(), method, target, post_body, header_params);
}

std::string get_status(std::string_view url) {
  auto raw_msg = request(docker::GET, url);
  if (raw_msg && (raw_msg->first == 200)) {
    return raw_msg->second;
  }
  return "UNKNOWN";
}

void create_udev_hw_files(std::string_view endpoint,
                          std::filesystem::path base_hw_db_path,
                          std::vector<std::pair<std::string, std::vector<std::string>>> udev_hw_db_entries) {
  auto url = fmt::format("{}/write-hwdb", endpoint);
  for (const auto &[filename, content] : udev_hw_db_entries) {
    auto host_file_path = (base_hw_db_path / filename).string();
    logs::log(logs::debug, "[HOOK] Writing hwdb file: {}", host_file_path);
    auto body = json::object();
    body["path"] = host_file_path;
    body["content"] = utils::join(content, "\n");
    auto raw_msg = request(docker::POST, url, false, json::serialize(body));
    if (raw_msg && (raw_msg->first == 200)) {
      logs::log(logs::info, "[HOOK] call write-hwdb {} hook: {} - {}", host_file_path, raw_msg->first, raw_msg->second);
    } else {
      logs::log(logs::error,
                "[HOOK] call write-hwdb {} hook error: {} - {}",
                host_file_path,
                raw_msg->first,
                raw_msg->second);
    }
  }
}

void RunHook::run(std::size_t session_id,
                  std::string_view app_state_folder,
                  std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
                  const immer::array<std::string> &virtual_inputs,
                  const immer::array<std::pair<std::string, std::string>> &paths,
                  const immer::map<std::string, std::string> &env_variables,
                  std::string_view render_node) {
  // Fake udev
  // auto udev_base_path = std::filesystem::path(app_state_folder) / "udev";
  auto udev_base_path = std::filesystem::path("/run") / "udev";
  auto hw_db_path = udev_base_path / "data";
  auto fake_udev_cli_path = std::string(utils::get_env("WOLF_DOCKER_FAKE_UDEV_PATH", ""));
  bool use_fake_udev = !fake_udev_cli_path.empty() || std::filesystem::exists(fake_udev_cli_path);
  if (use_fake_udev) {
    // logs::log(logs::info, "[HOOK] Using fake-udev, creating {}", hw_db_path.string());
    // std::filesystem::create_directories(hw_db_path);

    // Check if /run/udev/control exists
    /*auto udev_ctrl_path = udev_base_path / "control";
    if (!std::filesystem::exists(udev_ctrl_path)) {
      if (auto control_file = std::ofstream(udev_ctrl_path)) {
        control_file.close();
        std::filesystem::permissions(udev_ctrl_path, std::filesystem::perms::all); // set 777
      }
    }*/
    // mounts.push_back(MountPoint{.source = udev_base_path.string(), .destination = "/run/udev/", .mode = "rw"});
    // mounts.push_back(MountPoint{.source = fake_udev_cli_path, .destination = "/usr/bin/fake-udev", .mode = "ro"});
  } else {
    logs::log(logs::warning,
              "[HOOK] Unable to use fake-udev, check the env variable WOLF_DOCKER_FAKE_UDEV_PATH and the file at {}",
              fake_udev_cli_path);
  }

  logs::log(logs::info, "[HOOK] this->env: {}, this->endpoint: {}", this->env, this->endpoint);

  auto url = fmt::format("{}/start", this->endpoint);

  auto post_params = json::object();
  auto envs = json::object();
  for (const auto &env : this->env) {
    auto split = utils::split(env, '=');
    if (split.size() == 2) {
      envs[utils::to_string(split[0])] = utils::to_string(split[1]);
    } else {
      logs::log(logs::warning, "invalid env: {}", env);
    }
  }
  for (const auto &pair : env_variables) {
    envs[pair.first] = pair.second;
  }
  post_params["envs"] = envs;
  auto json_payload = json::serialize(post_params);

  logs::log(logs::info, "[HOOK] call start hook: {}: {}", url, json_payload);
  auto raw_msg = request(docker::POST, url, false, json_payload);
  if (raw_msg && (raw_msg->first == 201)) {
    logs::log(logs::info, "[HOOK] call start hook: {} - {}", raw_msg->first, raw_msg->second);
  } else {
    logs::log(logs::error, "[HOOK] call start hook error: {} - {}", raw_msg->first, raw_msg->second);
    return;
  }

  auto terminate_handler = this->ev_bus->register_handler<immer::box<events::StopStreamEvent>>(
      [session_id, this](const immer::box<events::StopStreamEvent> &terminate_ev) {
        if (terminate_ev->session_id == session_id) {
          // curl to stop
          logs::log(logs::info, "[HOOK] stop session {}", session_id);
          auto url = fmt::format("{}/stop", this->endpoint);
          auto raw_msg = request(docker::POST, url);
          if (raw_msg && (raw_msg->first == 202)) {
            logs::log(logs::info, "[HOOK] call start hook: {} - {}", raw_msg->first, raw_msg->second);
          } else {
            logs::log(logs::error, "[HOOK] call start hook error: {} - {}", raw_msg->first, raw_msg->second);
            return;
          }
        }
      });

  auto unplug_device_handler = this->ev_bus->register_handler<immer::box<events::UnplugDeviceEvent>>(
      [session_id, this](const immer::box<events::UnplugDeviceEvent> &ev) {
        logs::log(logs::info, "[HOOK] unplugin event session {}", session_id);
      });

  do {
    // Plug all devices that are waiting in the queue
    while (auto device_ev = plugged_devices_queue->pop(50ms)) {
      if (device_ev->get().session_id != session_id) {
        continue;
      }
      if (use_fake_udev) {
        create_udev_hw_files(this->endpoint, hw_db_path, device_ev->get().udev_hw_db_entries);
      }
      logs::log(logs::info, "[HOOK] plugin event session {}", session_id);
      for (auto udev_ev : device_ev->get().udev_events) {
        std::string cmd;
        std::string udev_msg = utils::base64_encode(utils::map_to_string(udev_ev));
        if (udev_ev.count("DEVNAME") == 0) {
          cmd = fmt::format("fake-udev -m {}", udev_msg);
        } else {
          cmd = fmt::format("mkdir -p /dev/input && mknod {} c {} {} && chmod 777 {} && fake-udev -m {}",
                            udev_ev["DEVNAME"],
                            udev_ev["MAJOR"],
                            udev_ev["MINOR"],
                            udev_ev["DEVNAME"],
                            udev_msg);
        }
        logs::log(logs::debug, "[HOOK] Executing command: {}", cmd);
        auto exec_body = json::object();
        exec_body["cmd"] = "/bin/bash";
        exec_body["args"] = {"-c", cmd};
        exec_body["user"] = "root";
        auto exec_msg =
            request(docker::POST, fmt::format("{}/exec", this->endpoint), false, json::serialize(exec_body));
        if (exec_msg && (exec_msg->first == 200)) {
          logs::log(logs::info, "[HOOK] call exec hook: {} - {}", exec_msg->first, exec_msg->second);
        } else {
          logs::log(logs::error, "[HOOK] call exec hook error: {} - {}", exec_msg->first, exec_msg->second);
        }
      }
    }
    std::this_thread::sleep_for(500ms);
  } while (get_status(fmt::format("{}/status", this->endpoint)) == "RUNNING");
  terminate_handler.unregister();
}

} // namespace wolf::core::hook
