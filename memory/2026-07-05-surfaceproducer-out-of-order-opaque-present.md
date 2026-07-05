# 2026-07-05 SurfaceProducer out-of-order / opaque present 记忆

## 硬目标

- 方向：KiriKiri-LauncherC 继续走 Flutter + SDL3，目标是把运行时呈现链路从 Cocos 迁出，不再依赖 Cocos 作为游戏画面/输入/音频主路径。
- 分辨率：游戏内部渲染目标固定为 `1920x1080`。不要再让 Android 物理窗口尺寸、横竖屏瞬时尺寸或 Flutter View 尺寸反推游戏 backbuffer 尺寸。
- 当前 Flutter/SDL3 启动证据见 `/root/log/20260705221717924.log`：
  - line 4: `MainActivity.onCreate#sdl-java version=3.4.10 libs=SDL3,main`
  - line 31/32: `SDL runtime compiled=3.4.10 linked=3.4.10 platform=Android`
  - line 56: `[flutter-surface] set game surface window=... size=1920x1080 requested=1920x1080`
  - line 58/59: `MainActivity.createGameSurfaceTexture id=0 mode=surface-producer size=1920x1080 requested=1920x1080`
  - line 1717: `LayerMgr::DrawBuffer#CREATED size=1920x1080 holdAlpha=1`
  - line 1720: `[android-egl-presenter] surface ready #1 ... size=1920x1080 fullFrame=1`
- 去 Cocos 的判断标准：不是“隐藏 Cocos 画面”就算完成，而是热路径中 KiriKiri 画面由 SDL3/Flutter surface 链路稳定 present，Cocos 不再持有最终画面节奏，也不再参与游戏 framebuffer 的关键同步。

## 最新日志证据

### `/root/log/20260705221717924.log`

这份 core 日志显示应用已经按 Flutter + SDL3 + SurfaceProducer 模式启动，并且请求/创建了固定 `1920x1080` surface：

- `22:17:19.441`：`[flutter-surface] set game surface ... size=1920x1080 requested=1920x1080`
- `22:17:19.442`：`MainActivity.createGameSurfaceTexture id=0 mode=surface-producer size=1920x1080 requested=1920x1080`
- `22:17:22.124`：`[sdl-screen] pump no-surface #1 ... copiedRegions=0 copiedBytes=0`
- `22:17:22.124`：`sdl-screen-takeover request supported=1 enabled=1 pumped=0 cocosHidden=0 frame=2780x1264 scene=2048x931`
- `22:17:22.130`：`LayerMgr::DrawBuffer#CREATED size=1920x1080 holdAlpha=1`
- `22:17:22.132`：`[android-egl-presenter] surface ready #1 ... size=1920x1080 fullFrame=1`
- `22:17:22.194`：`sdl-screen-takeover update-enforced cocosHidden=1`

这里有两个重要信号：

- 固定尺寸目标已经生效：core 侧 draw buffer 与 EGL presenter 都是 `1920x1080`。
- 仍然存在 alpha/透明链路风险：`LayerMgr::DrawBuffer#CREATED ... holdAlpha=1` 表示 draw buffer 保留 alpha。若最终 Android/Flutter surface 不是强制 opaque，全屏内容也可能被系统以带 alpha 图层合成，增加厂商 SurfaceFlinger/BLAST 异常概率。

### `/root/log/78.log`

系统 logcat 里同一轮启动出现大量 SurfaceFlinger out-of-order 报错，目标 layer 是本应用：

- line 46480 附近：`org.github.krkr2.MainActivity` resume/top resumed。
- line 46514：`updateBlastSurfaceIfNeeded ... format:-3, blastBufferQueue:null`，主窗口 BLAST surface 重新建立。
- line 46518/46519：`BufferQueueConsumer ... [VRI[MainActivity]#4(BLAST Consumer)4]` connect，并设置 acquired buffer count。
- line 46536：SurfaceView layer 创建：`SurfaceView[org.github.krkr2/org.github.krkr2.MainActivity] ... createBlastSurfaceControls`
- line 46537-46540：新 SurfaceView BLAST consumer connect。
- line 46561：`BLASTBufferQueue: [VRI[MainActivity]#4] ... mFrameNumber=1 ... transform=7`
- line 46583 开始：`SurfaceFlinger: Out of order buffers detected for RequestedLayerState{org.github.krkr2/org.github.krkr2.MainActivity#34791 parentId=34790} producedId=4 frameNumber=745 -> producedId=4 frameNumber=2`
- line 46604/46619/46655/... 持续重复同类错误，`frameNumber=745 -> frameNumber=3/4/5/...`，一直增长到至少 line 50800 的 `frameNumber=106`。

核心证据模式：SurfaceFlinger 认为同一个 `RequestedLayerState{org.github.krkr2/org.github.krkr2.MainActivity#34791}`、同一个 `producedId=4` 上，已经见过高帧号 `745`，随后又收到低帧号 `2` 起步的新 buffer。这个模式很像 producer/consumer 或 layer 复用时序错乱：旧 producer 的 frame counter/状态仍被 SurfaceFlinger 记住，新链路从 frame 1/2 重新出帧，导致系统判定 out-of-order。

## 当前判断

- 这不像单纯的 KiriKiri 内容绘制 bug。core 侧 `1920x1080` draw buffer 和 EGL presenter 已创建，但系统层在 Activity/SurfaceView/BLAST 切换阶段报 out-of-order。
- 高风险点是 SurfaceProducer 新路径与 Android View/BLAST/SurfaceView 生命周期叠加，特别是在 Activity relayout、SurfaceView 重建、TextureView/SurfaceTexture 或 Flutter texture attach/detach 之间，复用了旧 layer/producer 状态。
- `format:-3`、`holdAlpha=1`、Flutter texture/surface 默认透明、以及 Android SurfaceView/TextureView 对 alpha 的处理都可能让系统以非 opaque 合成路径处理主画面。即使画面视觉上全屏，透明格式也可能触发更多合成/重排行为。

## alpha / opaque 修复思路

目标规则：游戏画面是全屏不透明内容，最终 present 必须按 opaque 处理。不要依赖“实际像素 alpha 多半是 255”这种隐含事实。

- Native GLES/EGL present 前，确保输出纹理/默认 framebuffer 的 alpha 为 1.0。
- full-frame present 时必须覆盖完整 `1920x1080`，不留下未初始化或旧 alpha 区域。
- 对 SDL/Flutter/Android surface，尽可能声明 opaque：
  - Flutter `Texture`/`SurfaceProducer` 路径若有 opaque/alpha 参数，默认设为 opaque。
  - Android `SurfaceView`/`TextureView` 若保留，避免透明 pixel format；不要使用 translucent window 或透明 surface format 承载游戏主画面。
  - EGL config 选择优先无 alpha channel；如果平台/Flutter 路径只能给 RGBA，也要在 fragment 输出和 clear 中强制 `a=1.0`。
- 对 KiriKiri 内部 `holdAlpha=1` 要谨慎：它可能是脚本/层系统语义需要，不一定能直接关。但最终送到 Android compositor 的 game present surface 必须 opaque。也就是说可以内部保留 alpha，最终合成到 present target 时压成 alpha 1。

## SurfaceProducer 默认策略

当前日志显示默认走 `mode=surface-producer`。结合 SurfaceFlinger out-of-order，建议短期策略改为：

- SurfaceProducer 默认禁用，作为实验开关保留。
- 默认回退 legacy SurfaceTexture 路径，优先换稳定性，避免继续让所有用户命中厂商 SurfaceFlinger/BLAST out-of-order。
- SurfaceProducer 只有在满足以下条件时再开启：
  - 生命周期严格串行：旧 surface 完整 detach/release 后才创建新 producer。
  - 新 surface 不复用会让 SurfaceFlinger 混淆的 layer/producer 状态。
  - 每次 attach/recreate 都能明确丢弃旧 EGLSurface、旧 native window、旧 texture id。
  - 日志中连续启动/旋转/切后台恢复无 `Out of order buffers detected`。
- 开关建议可观测：日志必须打印 `mode=surface-producer` 或 `mode=legacy-surface-texture`，以及禁用原因、fallback 原因。

## AetherKiri present 规则

AetherKiri/Android EGL presenter 的 hard rule：

- 只有有新游戏帧时才 `swap`。没有新帧不要为了心跳、校验、等待 surface 或复用旧图而空 swap。
- 每次 swap 都必须是 full-frame opaque present：
  - full-frame：覆盖完整 `1920x1080` present target。
  - opaque：最终 alpha 恒为 1。
  - deterministic：不读旧 framebuffer 内容，不依赖上一帧残留。
- surface/context 变化时：
  - 先丢弃旧 `EGLSurface`/native window 绑定。
  - 新 surface ready 后，等待下一帧真实游戏内容。
  - 首帧必须 full-frame opaque，不允许只补 dirty region。
- dirty rect/copy-region 优化只能发生在内部 CPU/GPU 缓冲层，不能让最终 Android present 变成局部更新或透明叠加。对系统 compositor 来说，游戏层每次提交都应该是一张完整不透明图。

## 禁止 hot-path glReadPixels / 校验

- 禁止在热路径用 `glReadPixels` 做 alpha 校验、画面校验、脏区比较或“确认是否全屏”的逻辑。
- `glReadPixels` 会强制 GPU/CPU 同步，破坏 present 节奏，容易把 SurfaceFlinger/BLAST 的时序问题放大成卡顿、乱序或掉帧。
- 如需验证 alpha/opaque：
  - 用离线 debug 开关，单次/低频采样，不进入 release hot path。
  - 优先靠结构性保证：clear alpha=1、shader 输出 alpha=1、EGL config/Surface format opaque、full-frame draw。
  - 日志只记录结构性状态，例如 `fullFrame=1 opaque=1 surfaceMode=legacy-surface-texture`，不要读回像素。

## 后续实现优先级

1. 先把 SurfaceProducer 默认关掉，legacy SurfaceTexture 作为默认稳定路径。
2. 在 Android/Flutter/native present 三层补齐 opaque 声明或 alpha=1 保证。
3. AetherKiri presenter 改成“有新帧才 swap”并确保每次 swap full-frame opaque。
4. surface/context recreate 时强制丢旧对象，首帧只接受 full-frame opaque。
5. 增加轻量日志验证，不加入 hot-path `glReadPixels`。

这份记忆的约束很硬：不要为追求 SurfaceProducer 新路径而牺牲稳定性；不要为排查 alpha 而引入热路径读回；不要让固定 `1920x1080` 被 Android 窗口尺寸重新污染。

## 本次已落地的关键改动

实现时间：2026-07-05 深夜，针对用户反复反馈的“不开 OpenGL 精确渲染就出现上下/左右多个旧画面，只有最新区域是新的”。

### 1. SurfaceProducer 改为显式 opt-in

文件：`platforms/android/app/java/org/github/krkr2/MainActivity.kt`

- `isFlutterSurfaceProducerDisabled()` 从“默认启用 SurfaceProducer，只有 `KRKR2_DISABLE_FLUTTER_SURFACE_PRODUCER=1` 才关”改为“默认禁用 SurfaceProducer”。
- 新策略：
  - 设置 `KRKR2_DISABLE_FLUTTER_SURFACE_PRODUCER=1/true`：强制禁用。
  - 设置 `KRKR2_DISABLE_FLUTTER_SURFACE_PRODUCER=0/false`：显式允许旧行为。
  - 未设置 disable 时，只有 `KRKR2_ENABLE_FLUTTER_SURFACE_PRODUCER=1/true` 才启用。
  - 默认路径走 `createLegacySurfaceTextureTarget()`，日志应显示 `mode=surface-texture`。
- 原因：最新 `78.log` 出现同一 `producedId=4` 下 `frameNumber=745 -> 2..106` 的 SurfaceFlinger out-of-order 证据，和 SurfaceProducer 组合外部 EGL 写入高度吻合。

### 2. EGL presenter 改成 Aether 式 deferred swap

文件：`cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

- `TryPresentAndroidEGLSurfaceTexture()` 不再在 draw 后立即 `eglSwapBuffers()`。
- 新链路：
  - 绑定 Flutter/Android 的 EGL window surface。
  - full clear opaque black：`glClearColor(0, 0, 0, 1)` + `glClear(GL_COLOR_BUFFER_BIT)`。
  - 按固定 `1920x1080` target 计算 aspect viewport，绘制完整最终纹理。
  - fragment shader 输出 `vec4(color.rgb, 1.0)`，最终 alpha 恒为 1。
  - draw 成功后 `glFlush()`。
  - 调用新增 `MarkAndroidEGLFrameDirtyLocked(...)`，只设置：
    - `state.frameDirty = true`
    - `pendingNativeGL`
    - `pendingWidth = 1920`
    - `pendingHeight = 1080`
    - `pendingDirtyTexture = texture`
  - 真正 `eglSwapBuffers()` 交给 `TVPForceSwapBuffer()` 调用的 `SwapAndroidEGLSurfacePresenterIfDirty()`。
- 目的：完整搬 AetherKiri 的“UpdateDrawBuffer 只 MarkFrameDirty，TVPForceSwapBuffer 里 ConsumeFrameDirty 后才 swap”。避免没新帧时空 swap 双缓冲旧 backbuffer，也避免一帧内 immediate swap 与 Cocos/Flutter/SurfaceTexture 时序打架。
- `eglSwapInterval(0)` 也改成默认启用，匹配 AetherKiri 的 WindowSurface 思路；仍保留 `KRKR2_ANDROID_EGL_SWAP_INTERVAL_ZERO=0/false` 或 `KRKR2_DISABLE_ANDROID_EGL_SWAP_INTERVAL_ZERO=1/true` 作为回退开关。

### 3. 上层 present 记录配合 deferred swap

文件：`cpp/core/environ/sdl/SDLGameManager.cpp`

- Android EGL path 成功后现在 `result.deferredSwap = true`。
- `TVPSDLTryPresentTexture()` 对 deferred path 不再立刻递增 `presentedFrames`、不立刻 `TVPRuntimeRecordPresentFrame()`、不立刻 `texture->ConsumeDirtyRect()`。
- 现在会把 `TVPRuntimePresentFrameInfo` 写入 `gSDLScreenPresenterState.pendingExternalFrameInfo`。
- `TVPForceSwapBuffer()` 成功执行 `TVPSDLAndroidSwapExternalPresenterIfDirty()` 后，会调用 `TVPSDLRecordExternalPresenterPostedFrame()`，此时才：
  - 递增 `presentedFrames`
  - 给 pending frame info 填 sequence/valid
  - 记录 runtime present frame
  - 在 swap 函数内消费 texture dirty rect
- 这保证“记录/消费 dirty”与“系统实际收到一帧”一致。

### 4. opaque/alpha 修复

文件：`cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

- EGL fragment shader 最终强制 alpha 为 `1.0`。
- EGL full-frame draw 前强制 `glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE)`。
- GL state save/restore 增加 color mask，避免恢复时污染 Cocos/旧上下文。
- CPU fallback 写 Android buffer 时强制每个像素 alpha 为 `0xff`。
- `FillAndroidBufferBlack()` 改成 opaque black，不再 `memset(..., 0)` 写透明黑。

文件：`platforms/android/app/java/org/github/krkr2/MainActivity.kt`

- Flutter overlay 的 `FlutterTextureView` 改为 `setOpaque(true)`。
- Flutter root view 背景改为 `Color.BLACK`。

文件：`flutter_launcher/lib/src/pages/game_overlay_page.dart`

- `Texture` 增加 `ValueKey<int>(textureId)`，降低 texture id 切换时复用旧 element/旧 texture state 的概率。

### 5. 仍然不能破坏的底层约束

- 游戏画面 buffer 仍固定 `1920x1080`，不能改为铺满物理屏或 Flutter 逻辑尺寸。
- 最终可见 present 必须 full-frame deterministic present。dirty rect 只能用于内部上传/缓存，不能用于 Android compositor 可见层的局部 present。
- 不加入热路径 `glReadPixels`、checksum、像素完整性校验。
- SurfaceProducer 以后只能作为实验开关重新验证，默认稳定路径是 legacy SurfaceTexture。
