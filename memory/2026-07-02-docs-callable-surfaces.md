# 2026-07-02 `/root/kiriki-work/docs` 可调用接口与迁移记忆

范围：

- 只整理 `/root/kiriki-work/docs` 中对 `KiriKiri-LauncherC`
  Flutter + SDL3 迁移有直接调用价值的部分。
- `docs` 是参考文档目录，不修改。
- 下列接口有些是文档中的目标设计/教程示例，有些是当前项目中已有体系。
  使用前必须对照 `KiriKiri-LauncherC` 当前源码确认签名和命名。

硬性方向：

- 继续迁移到 Flutter + SDL3。
- 逐步移除 Cocos host，不再把 Cocos 作为最终架构依赖。
- 渲染热路径避免过量验证；优先采用参考项目和文档中成熟的持久资源、
  dirty rect、pitch-aware upload、FBO/Surface 边界设计。

## 1. 构建系统与 Android 入口

参考文档：

- `M02-项目构建系统深度解析`
- `M02/.../04-Android-Gradle与CMake协作`
- `P01-现代CMake与构建工具链`
- `P02-vcpkg包管理`

可用/可迁移点：

- CMake 目标分层：
  - `krkr2`：最终应用/共享库目标。
  - `krkr2core`：核心模块聚合。
  - `krkr2plugin`：静态插件库。
  - `core_*_module`：按 base/environ/visual/sound/movie 等模块组织。
- Android Gradle 通过 `externalNativeBuild.cmake` 调用 CMake。
- Android 需要关注：
  - `ndkVersion`
  - `compileSdk/minSdk/targetSdk`
  - `externalNativeBuild.cmake.targets`
  - ABI / CMake cache / vcpkg Android triplet
  - `org.gradle.jvmargs`，CI 中 C++ 编译可能需要较大堆。
- `JNI_OnLoad(JavaVM*, void*)` 是 native 库加载入口，可在这里做：
  - spdlog Android sink 初始化。
  - breakpad/minidump 初始化。
  - SDL JNI 初始化转发。文档写的是 SDL2；本项目迁移目标是 SDL3，做法要对照
    SDL3 Android 初始化要求，不照抄 SDL2 名称。

当前项目注意：

- 顶层 Android `krkr2` target 已显式链接 SDL3、EGL、GLES、Vulkan 等。
- 后续改 CMake 时保持 `renderer` 和 `graphics_backend` 概念分离：
  - `renderer`: `opengl` / `software`
  - `graphics_backend`: `opengl` / `vulkan` / `gpuapi`
  - 不要把 Vulkan/GPUAPI 写进 `renderer`。

## 2. PAL / 平台抽象层可调用函数

参考文档：

- `M03-平台抽象层解析/02-Platform接口与平台实现对比`
- `P03-跨平台C++开发`

核心契约来自 `Platform.h`。这些是迁移时应该继续调用或接到
Flutter/SDL3 后端上的平台能力：

- 内存：
  - `TVPGetMemoryInfo(TVPMemoryInfo &m)`
  - `TVPGetSystemFreeMemory()`
  - `TVPGetSelfUsedMemory()`
  - `TVPCheckMemory()`
- 对话框/输入：
  - `TVPShowSimpleMessageBox(...)`
  - `TVPShowSimpleMessageBoxYesNo(...)`
  - `TVPShowSimpleInputBox(...)`
  - Android/Flutter 迁移时这些应桥接到 Flutter dialog，而不是 Cocos form。
- 路径与存储：
  - `TVPGetDriverPath()`
  - `TVPGetAppStoragePath()`
  - `TVPCheckStartupPath(path)`
  - `TVPGetInternalPreferencePath()`
  - `TVPGetPackageVersionString()`
- 文件操作：
  - `TVPDeleteFile(filename)`
  - `TVPRenameFile(from, to)`
  - `TVPCopyFile(from, to)`
  - `TVP_stat(name, tTVP_stat &s)`
  - `TVP_utime(name, modtime)`
- 输入法：
  - `TVPShowIME(x, y, w, h)`
  - `TVPHideIME()`
  - Flutter 迁移时应接到 Flutter text input / platform channel。
- 系统：
  - `TVPRelinquishCPU()`
  - `TVPPrintLog(str)`
  - `TVPExitApplication(code)`
  - `TVPSendToOtherApp(filename)`

迁移原则：

- PAL 层是业务代码可调用边界，Flutter/SDL3 后端应实现/转接这些能力。
- 不要让业务层直接依赖 Flutter/Kotlin/Cocos 类型。
- Android 路径/权限仍需保留 SAF/外部存储处理逻辑。

## 3. Flutter Platform Channel / UI 互调

参考文档：

- `M13-UI框架替换实战/03-Flutter嵌入实战/02-PlatformChannel实现UI交互.md`
- `P12-现代跨平台UI/02-Flutter引擎嵌入`
- `P12-现代跨平台UI/04-UI与渲染引擎分离`

可调用/可迁移点：

- MethodChannel：Dart 调 native/C++ 并等待返回。
  - 适合：打开游戏、文件选择、偏好读写、菜单动作、诊断查询。
- EventChannel：native/C++ 向 Dart 推送流。
  - 适合：FPS/内存/渲染状态、加载进度、日志流、游戏状态事件。
- BasicMessageChannel：自定义二进制/低层消息。
  - 适合：高频但轻量的事件，或未来自定义 codec。
- Embedder/C++ 侧关键概念：
  - `FlutterPlatformMessage`
  - `reply_handle`
  - `FlutterEngineSendPlatformMessageResponse`
  - `StandardMethodCodec`
- 文档建议可封装：
  - `MethodCall { method, args }`
  - `MethodResult { success, value, error_code, error_message }`
  - `StandardMethodCodecDecoder::decodeMethodCall(...)`
  - `StandardMethodCodecDecoder::encodeResult(...)`
  - `encodeStringResult(...)`
  - `encodeError(...)`
  - `encodeNullResult(...)`

当前项目现实：

- Android 侧已有 Kotlin/Flutter MethodChannel 体系，不一定需要 C++ embedder 版
  `StandardMethodCodec`。
- 但迁移原则一致：所有 UI 操作应通过稳定 channel/router，不让 C++ 业务层知道
  Flutter widget 细节。

## 4. Flutter 外部纹理 / FBO / Surface 呈现

参考文档：

- `M13-UI框架替换实战/05-渲染引擎与UI的桥接/01-纹理共享方案与外部纹理.md`
- `M13/.../05-渲染引擎与UI的桥接/02-事件与生命周期同步.md`
- `P04-OpenGL图形编程`

可调用/可迁移点：

- Flutter OpenGL embedder 可通过 `FlutterOpenGLRendererConfig::fbo_callback`
  指定 Flutter 渲染目标 FBO。
- Flutter render 完成后 `present` 回调通知宿主可采样/合成该纹理。
- 文档里的 `FlutterRenderTarget` 目标设计可复用为未来统一 presenter：
  - `initialize()`
  - `destroy()`
  - `getBackFboId()`
  - `onFramePresented()`
  - `getFrontTextureId()`
  - `resize(width, height)`
  - `hasFrame()`
- 双缓冲思想：
  - Back buffer 给 Flutter 写。
  - Front buffer 给游戏/host 读。
  - present 后 swap，避免读写同一纹理。
- 共享/降级方案优先级：
  - Shared FBO / EGL shared context：零拷贝，但要求上下文共享正确。
  - EGLImage：跨 EGL context 共享。
  - AHardwareBuffer：Android 8+，OpenGL/Vulkan 可共享，长期值得评估。
  - GPU blit：非零拷贝但 GPU 内完成，作为兼容降级。
  - CPU readback/upload：最后兜底，热路径尽量避免。
- 同步：
  - EGL fence / texture barrier 用于跨上下文顺序保证。
  - 当前 Android EGL presenter 已经有 GL state snapshot/restore，后续继续保持。

当前项目现实：

- 当前走的是 Android Flutter `ANativeWindow` surface + EGL presenter。
- 已增加非 nativeGL texture 的 EGL software-upload fallback，避免掉到
  `ANativeWindow_lock` direct CPU 全帧写屏。
- 后续可把当前 presenter 继续抽象成渲染管理模块：
  - native GL texture fast path
  - software texture persistent GL upload path
  - SDL_GPU/Vulkan path
  - Flutter surface/window path

## 5. 输入事件与生命周期同步

参考文档：

- `M13/.../05-渲染引擎与UI的桥接/02-事件与生命周期同步.md`
- `P11-SDL2跨平台开发/02-窗口与事件循环`
- `P11/.../03-实战-SDL2在KrKr2中的角色`

可调用/可迁移点：

- Flutter 指针注入使用 `FlutterPointerEvent` 概念：
  - `struct_size`
  - `phase`
  - `timestamp`
  - `x/y`
  - `device_kind`
  - `device`
  - `scroll_delta_x/y`
  - `buttons`
- phase 状态机不能跳：
  - `kAdd -> kDown -> kMove* -> kUp -> kRemove`
  - 异常中断用 `kCancel`
- 时间戳用单调微秒：
  - Android/Linux: `clock_gettime(CLOCK_MONOTONIC)`
  - Windows: `QueryPerformanceCounter`
- 坐标：
  - Flutter 用逻辑像素。
  - SDL/Android 常见输入是物理像素或 window pixel，需要按 DPR/viewport 转换。
- 生命周期：
  - foreground/background 要通知 Flutter/SDL runtime。
  - low memory 应调用 Flutter 低内存通知并触发 KrKr compact/cache release。
  - surface changed 必须触发 presenter surface recreation/full-frame present。

当前项目现实：

- 已经有 SDL input queue / Flutter touch routing。
- 后续继续把 Cocos 事件源替换为 SDL3/Flutter host 事件源。
- 高频输入热路径只做必要坐标变换、coalescing、队列投递；不要加重验证。

## 6. UI 抽象接口

参考文档：

- `M13-UI框架替换实战/02-UI抽象接口设计`
- `P12-现代跨平台UI/04-UI与渲染引擎分离`
- `P12-现代跨平台UI/05-实战设计`

文档建议的可调用抽象：

- 基础：
  - `IUIElement::show()`
  - `IUIElement::hide()`
  - `IUIElement::isVisible()`
  - `IUIElement::destroy()`
  - `PointerEvent { Type Down/Move/Up/Cancel, x, y, pointerId }`
- 对话框：
  - `IDialog::setTitle(...)`
  - `IDialog::setMessage(...)`
  - `IDialog::setOnConfirm(...)`
  - `IDialog::setOnCancel(...)`
- 配置：
  - `IConfigDialog::setMasterVolume(...)`
  - `setBGMVolume(...)`
  - `setSEVolume(...)`
  - `setVoiceVolume(...)`
  - `setOnVolumeChanged(...)`
  - `setFullscreen(...)`
  - `setResolution(...)`
  - `setOnVideoSettingsChanged(...)`
- 游戏选择：
  - `IGameSelectDialog::setGameList(...)`
  - `setOnGameSelected(...)`
  - `setOnBrowse(...)`
- 文件选择：
  - `IFilePickerDialog` 系列，适合替换 Cocos 文件选择/Android SAF。

迁移原则：

- 业务逻辑依赖这些抽象，不依赖 Cocos/Flutter 具体控件。
- Flutter 实现层通过 PlatformChannel/状态管理响应接口命令。
- Cocos 实现只作为过渡兼容层，不再扩展。

## 7. RenderManager / Texture / OGL 可调用面

参考文档：

- `M04-渲染子系统/04-OGL后端/01-RenderManager架构.md`
- `M04/.../02-纹理管理与压缩.md`
- `M04/.../03-渲染流程与优化.md`
- `P04-OpenGL图形编程`

`iTVPRenderManager` 关键调用：

- 创建纹理：
  - `CreateTexture2D(pixel, pitch, w, h, format, flags)`
  - `CreateTexture2D(tTVPBitmap *bmp, flags)`
  - `CreateTexture2D(tTJSBinaryStream *stream, flags)`
  - `CreateTexture2D(iTVPTexture2D *orig, flags)`
- 渲染方法：
  - `GetRenderMethod(name)`
  - `CompileRenderMethod(name, body, nTextures, prefix)`
  - `GetOrCompileRenderMethod(...)`
- 绘制：
  - `OperateRect(...)`
  - `OperateTriangles(...)`
  - `OperatePerspective(...)`
- 状态/统计：
  - `IsSoftware()`
  - `GetName()`
  - `GetRenderStat(...)`
  - `BeginStencil(...)`
  - `EndStencil(...)`
  - `SetRenderTarget(...)`
- 注册：
  - `REGISTER_RENDERMANAGER(cls, name)`
  - `TVPRegisterRenderManager(...)`
  - `TVPGetRenderManager()`
  - `TVPIsSoftwareRenderManager()`

`iTVPTexture2D` 关键调用：

- 生命周期：
  - `AddRef()`
  - `Release()`
- 尺寸：
  - `GetWidth()`
  - `GetHeight()`
- CPU 像素：
  - `GetScanLineForRead(y)`
  - `GetScanLineForWrite(y)`
  - `Update(rect)`
  - `GetPoint(x, y)`
  - `SetPoint(x, y, color)`
- 属性：
  - `IsStatic()`
  - `IsOpaque()`
  - `GetScale()`
- GPU/UI 适配：
  - `GetAdapterTexture()`
  - 当前项目还扩展/使用 `GetNativeGLTextureId()` 和
    `PrepareTextureForExternalPresenter(...)`，这是 presenter 边界的重要改造点。
- 回收：
  - `RecycleProcess()`

OpenGL 上传/优化重点：

- `InternalUpdate(data, pitch, x, y, w, h)` 是 pitch-aware upload 的模型。
- 支持 `GL_UNPACK_ROW_LENGTH` 时按 pitch 设置 row length。
- 不支持时只在必要时 compact row copy。
- 尺寸不变用 `glTexSubImage2D`。
- 尺寸变化才 `glTexImage2D`。
- framebuffer fetch 可减少 target-as-source 拷贝。
- 大纹理可能走 `tTVPOGLTexture2D_split` 瓦片化；presenter 不应假设任意源纹理都有
  单一 native GL id。

## 8. 存储 / 归档 / IO 可调用面

参考文档：

- `M08-归档与IO系统/04-流与IO抽象层/01-tTJSBinaryStream体系.md`
- `M08/.../05-统一存储系统/01-存储媒体管理器.md`
- `M08/.../05-统一存储系统/03-TJS2-Storages类.md`

`tTJSBinaryStream` 必须实现：

- `Seek(offset, whence)`
- `Read(buffer, read_size)`
- `Write(buffer, write_size)`
- `GetSize()`
- 可选：`SetEndOfStorage()`

`tTJSBinaryStream` 已提供辅助：

- `GetPosition()`
- `SetPosition(pos)`
- `ReadBuffer(buffer, size)`
- `WriteBuffer(buffer, size)`
- `ReadI64LE()`
- `ReadI32LE()`
- `ReadI16LE()`
- `ReadI8LE()`

`iTVPStorageMedia` 必须实现：

- `GetName(name)`
- `NormalizeDomainName(name)`
- `NormalizePathName(name)`
- `CheckExistentStorage(name)`
- `Open(name, flags)`
- `GetListAt(name, iTVPStorageLister *lister)`
- `GetLocallyAccessibleName(name)`

统一入口：

- `TVPCreateStream(name, flags)`
  - read path 使用 `TVPGetPlacedPath` 搜索 auto path。
  - write path 使用 `TVPNormalizeStorageName`。
  - `>` 分隔归档路径，归档内文件只读。
  - 写/更新后要清理 auto path cache。
- `iTVPStorageLister::Add(file)` 用于目录枚举回调。

迁移用途：

- Flutter 文件选择和 Android SAF 最终都应转成 storage/path 接口，而不是绕过
  KrKr 的 auto path / archive 机制。
- 视频/音频/图像加载继续用 `tTJSBinaryStream`，不要直接把 Flutter/Android URI
  泄漏给解码器。

## 9. 插件 / TJS Native 绑定可调用面

参考文档：

- `M09-插件系统与开发`
- `M12-插件逆向与实现`
- `M07-TJS2脚本引擎/07-原生绑定`

插件模型：

- 原版动态插件入口：
  - `V2Link(iTVPFunctionExporter *exporter)`
  - `V2Unlink()`
- KrKr2 静态链接插件：
  - 插件编进 `krkr2plugin`。
  - `Plugins.link("xxx.dll"/"xxx.tpm")` 走虚拟加载/静态注册表。
  - 游戏脚本不需要改。

ncbind/TJS 原生类：

- `NCB_REGISTER_CLASS(MyClass)`：注册新 TJS 类。
- `NCB_ATTACH_CLASS(...)`：向已有类挂方法/属性。
- 底层关键 API/概念：
  - `TJSCreateNativeClassForPlugin(...)`
  - `TJSRegisterNativeClass(...)`
  - `TJSNativeClassSetClassID(...)`
  - `TJSNativeClassRegisterNCM(...)`
  - `TVPGetScriptDispatch()`
  - `iTJSDispatch2`
  - `tTJSVariant`
  - `TJS_STATICMEMBER`
  - `ncbAutoRegister`

迁移用途：

- Flutter/SDL3 架构不应破坏 TJS 插件可见性。
- 新 native 功能如渲染诊断、presenter 控制、内存预算，如果要暴露给脚本，
  应通过现有 TJS native binding 模式挂载，而不是从 UI 直接调用内部对象。

## 10. 音频 / 视频可调用面

参考文档：

- `M05-音频子系统`
- `M06-视频播放器`
- `P06-FFmpeg音视频开发`
- `P07-OpenAL音频编程`

音频解码器：

- `tTVPWaveDecoder`：
  - `GetFormat(tTVPWaveFormat &format)`
  - `Render(void *buf, tjs_uint bufsamplelen, tjs_uint &rendered)`
  - `SetPosition(tjs_uint64 samplepos)`
  - `DesiredFormat(const tTVPWaveFormat &format)` 可选。
- `tTVPWaveFormat`：
  - `SamplesPerSec`
  - `Channels`
  - `BitsPerSample`
  - `BytesPerSample`
  - `TotalSamples`
  - `TotalTime`
  - `SpeakerConfig`
  - `IsFloat`
  - `Seekable`

视频帧：

- `DVDVideoPicture` 是 FFmpeg 解码到渲染器之间的帧结构。
- 软件解码常用 `data[0..2]` 和 `iLineSize[0..2]`，lineSize/pitch 可能大于 width。
- `format` 常见为 `RENDER_FMT_YUV420P`。
- 帧字段：
  - `pts/dts`
  - `iWidth/iHeight`
  - `iDisplayWidth/iDisplayHeight`
  - `iFlags`
  - `color_matrix/color_range`
- 上传/转换必须尊重 stride/lineSize，不可按 width 直接整块拷贝。

迁移用途：

- 视频到纹理上传可参考当前 EGL software-upload 的 dirty/pitch 设计继续优化。
- 音频后端迁移到 SDL3/Oboe 时不要破坏 `tTVPWaveDecoder` 抽象。

## 11. SDL 可调用/迁移点

参考文档：

- `P11-SDL2跨平台开发`
- 当前项目已向 SDL3 迁移，文档中的 SDL2 名称/函数要按 SDL3 API 对照更新。

可复用思想：

- SDL 子系统按需初始化，不要一次性重启全栈。
- Window/event/render/audio 分层。
- 事件循环统一收敛：
  - SDL event queue 是跨平台输入源。
  - 自定义事件可用于跨线程唤醒。
  - 多窗口/Surface 需要明确 owner 和 lifecycle。
- OpenGL/EGL 与 SDL window/context 绑定要分清：
  - 创建/销毁上下文。
  - make current。
  - swap interval。
  - surface resize。
- Android 上 Flutter surface 与 SDL/engine GL context 的 current state 必须保存恢复。

当前项目用途：

- `SDLGameManager` 仍是迁移核心。
- 后续要继续把 Cocos input/presenter/UI form 栈替换为 SDL3 + Flutter runtime host。
- SDL_GPU / Vulkan / OpenGL presenter 应统一受 render manager/presenter manager 调度。

## 12. 立即可用于下一步开发的清单

优先级高：

1. Presenter/RenderManager 边界：
   - 将裸 `GetNativeGLTextureId()` 判定升级为 RenderManager 提供的
     `PrepareNativeGLTextureForExternalPresenter(texture, rect)` 一类接口。
   - 让 split texture/software texture/native GL texture 都由 RenderManager 给出
     稳定 presenter 输入。
2. EGL software upload：
   - 当前已经支持 dirty rect scratch + persistent GL texture。
   - 后续改成直接传 scanline + pitch 到 upload helper。
   - 有 `GL_UNPACK_ROW_LENGTH` 时避免逐行 compact copy。
3. Flutter UI：
   - 统一 MethodChannel/EventChannel 路由。
   - C++ 业务层只调用 UI 抽象，不碰 Flutter widget/Cocos node。
4. SDL3 host：
   - 继续移除 Cocos runtime host。
   - SDL3 event loop 作为输入和生命周期核心。
5. 内存/预算：
   - PAL 内存查询 + RenderManager `GetRenderStat` 形成 memory governor。
   - 纹理预算、graphic cache、archive cache 分级处理。

验证方向：

- 新日志应能区分：
  - `nativeGL=1 cpuCopyFree=1`
  - `nativeGL=0 softwareUpload=1`
  - direct CPU fallback
- Android 构建继续靠 GitHub Actions，本地当前缺少构建工具链。

