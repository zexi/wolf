# AI Debug 文档


## 复用 Lobbies 实现多 Session 复用出流

### 目标
- 移除 `resume` 中的 `SwitchStreamProducerEvents` 步骤
- 复用 lobbies 的代码和机制来实现多 session 复用出流
- 避免重复实现相同逻辑

### 当前问题分析

#### Resume 当前实现的问题
在 `src/moonlight-server/rest/endpoints.hpp:456-460` 中，resume 直接调用 `SwitchStreamProducerEvents`：
- **时机不对**：consumer pipeline 可能还未创建，handler 可能未注册，事件可能丢失
- **逻辑重复**：lobbies 已经实现了相同的流复用机制，不应该重复实现

#### Lobbies 的实现机制
Lobbies 通过以下方式实现流复用：
1. **CreateLobbyEvent**: 创建共享的 producer pipeline（使用 `lobby->id` 作为 interpipe 名称）
2. **JoinLobbyEvent**: 让 session 加入 lobby，通过 `SwitchStreamProducerEvents` 切换流源到 `lobby->id`
3. **LeaveLobbyEvent**: 让 session 离开 lobby，切换回自己的流源

Lobbies 的 handler (`src/moonlight-server/sessions/lobbies.cpp`) 已经实现了：
- 流切换逻辑（`SwitchStreamProducerEvents`）
- 输入设备切换（mouse、keyboard、touch_screen、joypads）
- 生命周期管理（自动停止 lobby 当所有人离开时）

### 实现方案

#### 方案：使用隐式 Lobby 复用流

使用基于 app 的隐式 lobby：
- **第一次 launch 时**：检查是否存在基于 `app_id` 的 lobby，如果不存在则创建隐式 lobby
- **后续 launch/resume 时**：让 session 加入已存在的隐式 lobby
- 完全复用 lobbies 的 join/leave 逻辑

#### 实现步骤

##### 步骤 1：移除 resume 中的 SwitchStreamProducerEvents
**文件**: `src/moonlight-server/rest/endpoints.hpp`
- 删除第 456-460 行的 `SwitchStreamProducerEvents` 调用

##### 步骤 2：在 launch 时创建隐式 Lobby
**文件**: `src/moonlight-server/rest/endpoints.hpp` (launch 函数)

在 launch 时（第一次启动应用）：
1. 查找是否存在基于 `app->base.id` 的 lobby
   - Lobby ID 格式：`fmt::format("app_{}", app_id)`
2. 如果不存在，创建一个隐式 lobby（复用 `CreateLobbyEvent`）
   - 使用 `app` 的配置（video_settings、audio_settings 等）
   - 从 request headers 获取 display_mode、audio_channel_count 等
   - 设置 `multi_user = true`，`stop_when_everyone_leaves = true`
3. 如果存在，让 new_session 加入该 lobby（复用 `JoinLobbyEvent`）

##### 步骤 3：在 resume 时加入已存在的 Lobby
**文件**: `src/moonlight-server/rest/endpoints.hpp` (resume 函数)

在 resume 时（恢复之前的 session）：
1. 查找基于 `old_session->app->base.id` 的 lobby
   - Lobby ID 格式：`fmt::format("app_{}", app_id)`
2. 如果 lobby 存在，让 new_session 加入该 lobby（复用 `JoinLobbyEvent`）
3. 如果 lobby 不存在（异常情况），记录警告日志

##### 步骤 4：复用 Lobbies 的 Handler
**文件**: `src/moonlight-server/sessions/lobbies.cpp`
- **无需修改**，lobbies 的 handler 已经处理了 `JoinLobbyEvent` 和 `CreateLobbyEvent`
- 这些 handler 会自动处理：
  - 流切换（`SwitchStreamProducerEvents`）
  - 输入设备切换（mouse、keyboard、touch_screen、joypads）
  - 生命周期管理

##### 步骤 5：处理 Session 生命周期
**文件**: `src/moonlight-server/rest/endpoints.hpp` (launch 和 resume 函数)
- 在创建 new_session 后，触发 `JoinLobbyEvent` 而不是直接调用 `SwitchStreamProducerEvents`
- 让 lobbies 的 handler 处理所有切换逻辑

### 具体实现细节

#### 1. 修改 launch 函数（创建隐式 Lobby）

```cpp
void launch(...) {
  // ... 现有代码 ...
  
  auto app = state::get_moonlight_app_by_id(...);
  auto new_session = create_run_session(...);
  
  // 查找或创建基于 app 的隐式 lobby
  auto app_id = app->base.id;
  auto lobby_id = fmt::format("app_{}", app_id);
  
  auto lobbies = state->lobbies->load();
  auto existing_lobby = state::get_lobby_by_id(lobbies.get(), lobby_id);
  
  if (!existing_lobby) {
    // 第一次 launch，创建隐式 lobby（复用 CreateLobbyEvent）
    auto display_mode_str = utils::split(get_header(headers, "mode").value_or("1920x1080x60"), 'x');
    auto surround_info = std::stoi(get_header(headers, "surroundAudioInfo").value_or("196610"));
    int channelCount = surround_info & (0xffff);
    
    auto create_lobby_ev = events::CreateLobbyEvent{
      .id = lobby_id,
      .name = fmt::format("Implicit lobby for {}", app_id),
      .profile_id = "",  // 隐式 lobby 不需要 profile
      .icon_png_path = app->base.icon_png_path,
      .multi_user = true,
      .stop_when_everyone_leaves = true,
      .pin = std::nullopt,
      .video_settings = {
        .width = std::stoi(display_mode_str[0].data()),
        .height = std::stoi(display_mode_str[1].data()),
        .refresh_rate = std::stoi(display_mode_str[2].data()),
        .wayland_render_node = app->render_node,
        .runner_render_node = app->render_node,
        .video_producer_buffer_caps = app->video_producer_buffer_caps
      },
      .audio_settings = {
        .channel_count = channelCount
      },
      .client_settings = current_client.settings,
      .runner_state_folder = new_session->app_local_state_folder,
      .runner = app->runner
    };
    state->event_bus->fire_event(immer::box<events::CreateLobbyEvent>(create_lobby_ev));
    
    // 等待 lobby 创建完成（可选，异步处理）
    // create_lobby_ev.on_setup_over->get_future().wait();
  }
  
  // 让 new_session 加入 lobby（复用 JoinLobbyEvent）
  // lobbies 的 handler 会自动处理流切换、输入设备切换等
  auto join_lobby_ev = events::JoinLobbyEvent{
    .lobby_id = lobby_id,
    .moonlight_session_id = new_session->session_id,
    .pin = std::nullopt
  };
  state->event_bus->fire_event(immer::box<events::JoinLobbyEvent>(join_lobby_ev));
  
  // 等待加入完成并检查错误（可选，异步处理）
  // auto error_msg = join_lobby_ev.error_message->get_future().get();
  // if (!error_msg.empty()) {
  //   logs::log(logs::error, "[LAUNCH] Failed to join lobby: {}", error_msg);
  // }
  
  // ... 现有代码：触发 StreamSession 事件、更新 running_sessions ...
}
```

#### 2. 修改 resume 函数（加入已存在的 Lobby）

```cpp
void resume(...) {
  auto old_session = state::get_session_by_client(...);
  if (old_session) {
    auto new_session = create_run_session(...);
    
    // 移除 SwitchStreamProducerEvents 调用（第 456-460 行）
    
    // 查找基于 app 的隐式 lobby（应该已经存在）
    auto app_id = old_session->app->base.id;
    auto lobby_id = fmt::format("app_{}", app_id);
    
    auto lobbies = state->lobbies->load();
    auto existing_lobby = state::get_lobby_by_id(lobbies.get(), lobby_id);
    
    if (existing_lobby) {
      // Lobby 已存在，让 new_session 加入（复用 JoinLobbyEvent）
      auto join_lobby_ev = events::JoinLobbyEvent{
        .lobby_id = lobby_id,
        .moonlight_session_id = new_session->session_id,
        .pin = std::nullopt
      };
      state->event_bus->fire_event(immer::box<events::JoinLobbyEvent>(join_lobby_ev));
      
      // 等待加入完成并检查错误（可选）
      // auto error_msg = join_lobby_ev.error_message->get_future().get();
      // if (!error_msg.empty()) {
      //   logs::log(logs::error, "[RESUME] Failed to join lobby: {}", error_msg);
      // }
    } else {
      // Lobby 不存在（异常情况），记录警告
      logs::log(logs::warning, "[RESUME] Lobby {} not found for app {}, session will use its own stream", 
                lobby_id, app_id);
    }
    
    // 更新 running_sessions
    state->running_sessions->update([&old_session, new_session](...) {
      return state::remove_session(ses_v, old_session.value()).push_back(*new_session);
    });
  }
}
```

#### 2. 复用 Lobbies 的代码

关键复用点：
- **`JoinLobbyEvent` handler** (`lobbies.cpp:176-236`): 
  - 自动处理 `SwitchStreamProducerEvents`（第 232-234 行）
  - 自动切换输入设备到 lobby 的 wayland_display（第 204-208 行）
  - 自动处理 joypads 的切换（第 210-229 行）

- **`CreateLobbyEvent` handler** (`lobbies.cpp:73-173`):
  - 自动创建 producer pipeline（第 96-106 行）
  - 自动创建 wayland_display（第 108-148 行）
  - 自动创建 audio_sink（第 151-172 行）

- **`LeaveLobbyEvent` handler** (`lobbies.cpp:239-254`):
  - 自动处理离开逻辑
  - 自动切换回 session 自己的流源

#### 3. 生命周期管理

- 当最后一个 session 离开时，lobby 自动停止（如果设置了 `stop_when_everyone_leaves = true`）
- 完全复用 lobbies 的生命周期管理逻辑（`lobbies.cpp:257-282`）

### 优势

1. **代码复用**: 完全复用 lobbies 的代码，不重复实现
2. **逻辑一致**: 使用相同的事件机制和 handler
3. **维护简单**: 流复用逻辑集中在一个地方（lobbies.cpp）
4. **功能完整**: 自动获得 lobbies 的所有功能（输入切换、设备管理等）

### 注意事项

1. **Lobby ID 生成**: 确保基于 `app_id` 的 `lobby_id` 是唯一的
2. **Session 替换**: resume 时 `old_session` 会被 `new_session` 替换，需要确保 lobby 中的 `session_id` 更新
3. **隐式 Lobby**: 这些 lobby 是隐式的（不通过 API 创建），但使用相同的机制
4. **Lobby 创建时机**: 可能需要等待 lobby 创建完成后再加入，或者使用异步方式

### 需要修改的文件

1. **`src/moonlight-server/rest/endpoints.hpp`**: 
   - 移除 `SwitchStreamProducerEvents` 调用（第 456-460 行）
   - 添加 lobby 查找/创建逻辑
   - 添加 `JoinLobbyEvent` 触发

2. **`src/moonlight-server/state/sessions.hpp`** (可选):
   - 可能需要添加基于 `app_id` 查找 lobby 的辅助函数

### 不需要修改的文件

- **`src/moonlight-server/sessions/lobbies.cpp`**: 完全复用现有代码
- **`src/moonlight-server/streaming/streaming.cpp`**: 流切换逻辑已存在
- **`src/moonlight-server/events/events.hpp`**: 事件定义已存在

### 实现检查清单

- [ ] 移除 `resume` 函数中的 `SwitchStreamProducerEvents` 调用
- [ ] 在 `launch` 函数中实现基于 `app_id` 的 lobby 查找逻辑
- [ ] 在 `launch` 函数中实现隐式 lobby 创建（复用 `CreateLobbyEvent`）
- [ ] 在 `launch` 函数中实现 session 加入 lobby（复用 `JoinLobbyEvent`）
- [ ] 在 `resume` 函数中实现 lobby 查找和加入逻辑
- [ ] 测试 launch 功能是否正常工作（创建 lobby 和加入）
- [ ] 测试 resume 功能是否正常工作（加入已存在的 lobby）
- [ ] 测试多个 session 是否能正确复用流
- [ ] 测试 session 离开时 lobby 是否正确清理
