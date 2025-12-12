# AI Debug 文档

## 多客户端共享流 + 独立 Session 设计

### 需求
1. **不同客户端能够共享出流**：多个客户端观看同一个视频/音频流，避免重复编码
2. **每个客户端是单独的 session**：每个客户端有独立的输入设备、显示设置等

### 当前架构分析

#### 现有结构
- `StreamSession`: 包含所有会话信息（显示模式、音频通道、输入设备、wayland display、audio sink 等）
- `VideoSession`: 从 `StreamSession` 创建，管理视频编码和发送到特定 `client_ip:client_port`
- `AudioSession`: 从 `StreamSession` 创建，管理音频编码和发送到特定 `client_ip:client_port`
- 每个 session 都有独立的 video/audio pipeline，直接发送到客户端

#### 问题
- 每个客户端创建独立的 session，导致重复编码（浪费 CPU/GPU）
- 无法实现真正的流共享

### 设计方案

#### 架构分离

```
┌─────────────────────────────────────────────────────────┐
│                  StreamSession (共享)                    │
│  - wayland_display (共享)                                │
│  - audio_sink (共享)                                      │
│  - video_context (共享)                                   │
│  - video_producer_pipeline (共享，只编码一次)            │
│  - audio_producer_pipeline (共享，只编码一次)            │
│  - display_mode (主客户端设置)                           │
│  - app (共享)                                             │
└─────────────────────────────────────────────────────────┘
                        │
                        │ 1:N
                        │
        ┌───────────────┼───────────────┐
        │               │               │
┌───────▼──────┐ ┌──────▼──────┐ ┌──────▼──────┐
│ClientSession1│ │ClientSession2│ │ClientSession3│
│              │ │              │ │              │
│- session_id  │ │- session_id  │ │- session_id  │
│- client_ip   │ │- client_ip   │ │- client_ip   │
│- mouse       │ │- mouse       │ │- mouse       │
│- keyboard    │ │- keyboard    │ │- keyboard    │
│- joypads     │ │- joypads     │ │- joypads     │
│- display_mode│ │- display_mode│ │- display_mode│
│- video_sink  │ │- video_sink  │ │- video_sink  │
│- audio_sink  │ │- audio_sink  │ │- audio_sink  │
└──────────────┘ └──────────────┘ └──────────────┘
```

#### 数据结构设计

##### 1. StreamSession (共享流会话)
```cpp
struct StreamSession {
  // 共享资源
  std::shared_ptr<immer::atom<virtual_display::wl_state_ptr>> wayland_display;
  std::shared_ptr<immer::atom<std::shared_ptr<audio::VSink>>> audio_sink;
  std::shared_ptr<immer::atom<gst_video_context::gst_context_ptr>> video_context;
  
  // 应用信息
  std::shared_ptr<App> app;
  std::string app_local_state_folder;
  std::string app_host_state_folder;
  
  // 流配置（主客户端设置，其他客户端可能需要适配）
  moonlight::DisplayMode primary_display_mode;  // 主客户端显示模式
  int audio_channel_count;
  
  // 编码 pipeline（共享，只创建一次）
  std::shared_ptr<GstElement> video_producer_pipeline;  // 视频编码 pipeline
  std::shared_ptr<GstElement> audio_producer_pipeline;  // 音频编码 pipeline
  
  // 客户端列表
  std::shared_ptr<immer::atom<immer::vector<ClientSession>>> connected_clients;
  
  // 流会话 ID
  std::size_t stream_session_id;
};
```

##### 2. ClientSession (客户端会话)
```cpp
struct ClientSession {
  // 客户端标识
  std::size_t session_id;  // 唯一的客户端 session ID
  std::string client_ip;
  std::string rtsp_fake_ip;
  
  // 加密密钥（每个客户端独立）
  std::string aes_key;
  std::string aes_iv;
  std::array<char, 16> rtp_secret_payload;
  uint32_t enet_secret_payload;
  
  // 输入设备（每个客户端独立）
  std::shared_ptr<std::optional<MouseTypes>> mouse;
  std::shared_ptr<std::optional<KeyboardTypes>> keyboard;
  std::shared_ptr<std::optional<TouchScreenTypes>> touch_screen;
  std::shared_ptr<immer::atom<JoypadList>> joypads;
  std::shared_ptr<std::optional<input::PenTablet>> pen_tablet;
  
  // 客户端设置
  immer::box<wolf::config::ClientSettings> client_settings;
  
  // 显示模式（客户端可能要求不同的分辨率/帧率）
  moonlight::DisplayMode display_mode;
  
  // 流端口
  unsigned short video_stream_port;
  unsigned short audio_stream_port;
  unsigned short control_stream_port;
  
  // 关联的流会话
  std::size_t stream_session_id;
  
  // UDP sockets（每个客户端独立）
  std::shared_ptr<udp::socket> video_socket;
  std::shared_ptr<udp::socket> audio_socket;
};
```

#### 流分发机制

##### 视频流分发
1. **Producer Pipeline**（共享，只创建一次）：
   ```
   waylandsrc -> video/x-raw -> encoder -> interpipesink name="stream_{stream_session_id}_video"
   ```

2. **Consumer Pipelines**（每个客户端一个）：
   ```
   interpipesrc listen-to="stream_{stream_session_id}_video" -> 
   videoconvertscale (如果需要适配分辨率) -> 
   rtpmoonlightpay -> 
   appsink -> UDP发送到 client_ip:client_port
   ```

##### 音频流分发
1. **Producer Pipeline**（共享，只创建一次）：
   ```
   pulsesrc device="virtual_sink_{stream_session_id}.monitor" -> 
   audio/x-raw -> 
   opusenc -> 
   interpipesink name="stream_{stream_session_id}_audio"
   ```

2. **Consumer Pipelines**（每个客户端一个）：
   ```
   interpipesrc listen-to="stream_{stream_session_id}_audio" -> 
   rtpmoonlightpay_audio -> 
   appsink -> UDP发送到 client_ip:client_port
   ```

#### 关键实现点

##### 1. Session 创建流程

**第一个客户端（创建流会话）**：
```
launch/resume -> 
  检查是否存在 stream_session_id（通过 app 或其他标识） ->
  不存在 -> 创建 StreamSession + ClientSession ->
  启动 video_producer_pipeline 和 audio_producer_pipeline ->
  启动该客户端的 consumer pipelines
```

**后续客户端（加入现有流会话）**：
```
launch/resume -> 
  检查是否存在 stream_session_id ->
  存在 -> 创建新的 ClientSession，关联到现有 StreamSession ->
  启动该客户端的 consumer pipelines（复用 producer pipelines）
```

##### 2. 流会话管理

- **StreamSession 查找**：通过 `app_id` 或其他标识查找现有的 `StreamSession`
- **StreamSession 生命周期**：当最后一个客户端断开时，销毁 `StreamSession` 和 producer pipelines
- **客户端管理**：维护 `StreamSession.connected_clients` 列表

##### 3. 显示模式适配

- **主客户端**：设置 `StreamSession.primary_display_mode`
- **其他客户端**：如果要求不同的分辨率/帧率，在 consumer pipeline 中使用 `videoconvertscale` 适配
- **限制**：所有客户端共享同一个 wayland display，所以实际渲染分辨率是主客户端的设置

##### 4. 输入处理

- 每个客户端有独立的输入设备（mouse、keyboard 等）
- 所有输入都发送到同一个 wayland display（共享）
- 需要处理输入冲突（例如多个鼠标同时移动）

##### 5. RTSP 匹配

- 每个 `ClientSession` 有独立的 `rtsp_fake_ip`
- RTSP 请求通过 `rtsp_fake_ip` 匹配到对应的 `ClientSession`
- 从 `ClientSession` 获取关联的 `StreamSession`

##### 6. ENET 控制流

- 每个客户端有独立的 ENET 连接
- 控制消息（输入、IDR 请求等）路由到对应的 `ClientSession`
- IDR 请求应该触发所有 consumer pipelines 的 IDR（如果需要）

#### 需要修改的文件

1. **`src/moonlight-server/events/events.hpp`**：
   - 添加 `ClientSession` 结构
   - 修改 `StreamSession` 结构（分离共享资源和客户端特定资源）

2. **`src/moonlight-server/state/sessions.hpp`**：
   - 添加 `create_stream_session()` 和 `create_client_session()` 函数
   - 修改 session 查找逻辑

3. **`src/moonlight-server/rest/endpoints.hpp`**：
   - 修改 `launch()` 和 `resume()` 函数，支持查找/创建 `StreamSession`
   - 修改 session 管理逻辑

4. **`src/moonlight-server/streaming/streaming.cpp`**：
   - 修改 `start_streaming_video()` 和 `start_streaming_audio()`，支持 producer/consumer 模式
   - 实现流分发机制

5. **`src/moonlight-server/rtsp/net.hpp`**：
   - 修改 `get_session()`，通过 `rtsp_fake_ip` 匹配 `ClientSession`

6. **`src/moonlight-server/control/control.cpp`**：
   - 修改 ENET 连接处理，关联到 `ClientSession`

7. **`src/moonlight-server/sessions/moonlight.cpp`**：
   - 修改事件处理，支持 producer/consumer 模式

#### 实现步骤

1. **Phase 1: 数据结构重构**
   - 定义 `ClientSession` 结构
   - 重构 `StreamSession` 结构
   - 更新所有相关的类型定义

2. **Phase 2: Session 管理**
   - 实现 `StreamSession` 查找和创建逻辑
   - 实现 `ClientSession` 创建和关联逻辑
   - 实现 session 生命周期管理

3. **Phase 3: 流分发**
   - 实现 producer pipeline（共享）
   - 实现 consumer pipeline（每个客户端）
   - 实现流分发机制

4. **Phase 4: 输入处理**
   - 确保每个客户端有独立的输入设备
   - 处理输入冲突

5. **Phase 5: 测试和优化**
   - 测试多客户端连接
   - 测试流共享
   - 性能优化

#### 注意事项

1. **显示模式冲突**：所有客户端共享同一个 wayland display，实际渲染分辨率是主客户端的设置。其他客户端如果需要不同分辨率，需要在 consumer pipeline 中缩放。

2. **音频混音**：如果多个客户端同时输出音频，可能需要混音处理。

3. **性能考虑**：
   - Producer pipeline 只创建一次，节省编码资源
   - Consumer pipelines 只做格式转换和打包，开销较小
   - 需要监控 producer pipeline 的性能

4. **错误处理**：
   - Producer pipeline 失败时，所有客户端都会受影响
   - 需要实现 producer pipeline 的重启机制

5. **流控制**：
   - 暂停/恢复应该影响所有客户端
   - 客户端断开时，只销毁该客户端的 consumer pipelines

#### 扩展功能（可选）

1. **动态分辨率切换**：允许主客户端切换分辨率，其他客户端自动适配
2. **客户端优先级**：某些客户端可以优先使用资源
3. **流质量自适应**：根据网络状况调整流质量
4. **多显示器支持**：不同客户端可以观看不同的显示器
