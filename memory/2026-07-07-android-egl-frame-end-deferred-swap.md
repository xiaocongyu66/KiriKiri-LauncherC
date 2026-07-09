# 2026-07-07 Android EGL frame-end deferred swap restored

## 背景

- 旧的 immediate swap 版本会在 `BasicDrawDevice::Show` 里直接 `eglSwapBuffers`。
- 这样能把 `dirtyOverwrites` 降到 0，但也会把同一 engine tick 内的中间帧暴露给 Flutter/Android surface。
- 用户截图中的特殊 CG 两侧残留人物碎片、上下/左右旧画面，更像“中间合成结果被提前上屏”，而不是最终完整帧本身正确后再显示错误。
- AetherKiri 的模式是：绘制完整 frame，标记 dirty；frame end / host pump 看到 dirty 后才 swap。

## 本轮改动

文件：

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

改动：

- `TryPresentAndroidEGLSurfaceTexture()` 不再 immediate `eglSwapBuffers`。
- EGL presenter 仍在 producer 线程完成全帧 blit、清屏、viewport、quad draw，然后 `glFlush()` 并 `MarkAndroidEGLFrameDirtyLocked()`。
- `TVPSDLTexturePresentResult::deferredSwap` 对 Android EGL 恢复为 `true`。
- `SDLGameManager.cpp` 不再在 `TVPSDLTryPresentTexture()` 内立刻 drain dirty swap。
- 唯一正常 swap owner 回到 frame-end pump：
  - `TVPSDLPumpScreenPresenter()`
  - `TVPSDLAndroidFlutterPresenterSwapIfDirty()`
  - `TVPSDLRecordExternalPresenterPostedFrame()`
- EGL blit shader 从单纯 `uUvScale` 改成 `uUvRect`，采样范围显式限定为有效 logical texture rect，后续如果处理 2048 backing padding 可以直接调 uv rect，不需要改 shader ABI。
- Android EGL presenter 与游戏 OpenGL 渲染共用 context 时，默认保存/恢复 GL state。现在会恢复 framebuffer、viewport、scissor、program、active texture、buffer binding、blend equation/function、depth/cull/scissor/stencil enable、depth mask、color mask、clear color 和 presenter 用到的 vertex attrib 状态。需要压测时可用 `KRKR2_DISABLE_ANDROID_EGL_SAVE_GL_STATE=1` 关闭。

## 期望日志形态

- producer 阶段：
  - `android-egl-presenter] queue-android-egl ... fullFrame=1`
  - `sdl-sync] queued ... path=egl ...`
- frame-end 阶段：
  - `android-egl-presenter] swap-android-egl ... stage=main-scene-update` 或 native frame-end pump stage
  - `sdl-sync] posted ...`
- `sync-overwrite-android-egl` 可能在一个 tick 内出现，但不再等同于可见错误；它只表示 pending backbuffer 被更晚的完整 frame 覆盖，最后由 frame-end swap 显示最新完整帧。

## 固定分辨率要求

- 保持游戏 surface/output 为 `1920x1080`。
- 不采用动态物理分辨率 surface。
- 如果后续参考 KrKr2-Next 的 resize/lifecycle，只借鉴生命周期，不搬动态尺寸策略。

## 本地验证状态

- `git diff --check` 通过。
- 本容器仍缺 `ANDROID_HOME` 和 `cmake`，无法在本地完成 Android/native 编译。
- `/root/kiriki-work/AetherKiri` 已执行 `git pull --ff-only`，结果为 `Already up to date.`。
