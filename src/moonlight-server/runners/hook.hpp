#pragma once

#include "state/serialised_config.hpp"
#include <events/events.hpp>
#include <immer/box.hpp>
#include <memory>
#include <utility>

namespace wolf::core::hook {

using namespace std::chrono_literals;
using namespace ranges::views;
using namespace wolf::core;

class RunHook : public events::Runner {
public:
  RunHook(std::shared_ptr<events::EventBusType> ev_bus, std::vector<std::string> env, std::string endpoint)
      : ev_bus(std::move(ev_bus)), env(std::move(env)), endpoint(std::move(endpoint)) {}

  void run(std::string_view session_id,
           std::string_view app_state_folder,
           std::string_view host_xdg_runtime_dir,
           std::shared_ptr<events::devices_atom_queue> plugged_devices_queue,
           const immer::array<std::string> &virtual_inputs,
           const immer::array<std::pair<std::string, std::string>> &paths,
           const immer::map<std::string, std::string> &env_variables,
           std::string_view render_node) override;

  events::RunnerTypes serialize() const override {
    return wolf::config::AppHook{.env = env, .endpoint = endpoint};
  }

private:
  std::shared_ptr<events::EventBusType> ev_bus;
  std::vector<std::string> env;
  std::string endpoint;
};

} // namespace wolf::core::hook
