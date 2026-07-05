# 2026-07-06 Aether-style frame-end swap and SDL host cut

## Hard target

- Final architecture remains Flutter + SDL3.
- Cocos2d-x is compatibility only and must not own final presentation.
- Native/game surface stays fixed at `1920x1080`.
- Final Android presentation must be one deterministic full-frame submit.
- Dirty rects are allowed only as upload/cache/new-frame hints.
- Do not add hot-path pixel validation, checksums, `glReadPixels`, or repeated
  heavy `glGet*` checks.

## Reference chain copied conceptually from AetherKiri

AetherKiri's complete chain is:

1. Engine produces a frame.
2. `HostWindowLayer::UpdateDrawBuffer()` clears the full target, computes the
   aspect viewport, draws one full textured quad, flushes, and marks the frame
   dirty.
3. `TVPForceSwapBuffer()` swaps only if the dirty bit is set.
4. After successful `eglSwapBuffers`, the dirty bit is consumed.

Important reference files:

- `/root/kiriki-work/AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp`
  - `HostWindowLayer::UpdateDrawBuffer`
- `/root/kiriki-work/AetherKiri/cpp/core/environ/android/AndroidUtils.cpp`
  - `TVPForceSwapBuffer`
- `/root/kiriki-work/AetherKiri/cpp/core/environ/EngineLoop.cpp`
  - `TVPDrawSceneOnce`

## Changes made in LauncherC

### Deferred swap is no longer drained inside `PresentTexture`

File:

- `cpp/core/environ/sdl/SDLGameManager.cpp`

Change:

- Android EGL presenter still receives the full-frame texture plan from
  `TVPSDLTryPresentTexture`.
- `TVPSDLTryPresentTexture` now queues the external EGL frame and logs
  `defer-swap`, but it does not call
  `TVPSDLAndroidFlutterPresenterSwapIfDirty()` immediately.
- This aligns with AetherKiri: present call draws/marks dirty; frame boundary
  performs the one swap.

Why:

- Previous code could swap during `BasicDrawDevice::Show`, before the host frame
  boundary. That was not Aether-style and could produce old/new buffer
  alternation under Android double buffering.
- User-visible symptom: when `ogl_accurate_render` is OFF, screenshots show one
  part of the screen from the newest frame and another part from stale content.

### Runtime presenter pump now owns Android EGL dirty-gated swap

File:

- `cpp/core/environ/sdl/SDLGameManager.cpp`

Change:

- `TVPSDLPumpScreenPresenter(stage)` now first tries
  `TVPSDLAndroidFlutterPresenterSwapIfDirty(stage)`.
- If swapped, it records the posted frame through
  `TVPSDLRecordExternalPresenterPostedFrame()` and returns.
- If no Android EGL dirty frame exists, it falls back to the older SDL surface
  mirror path.

Why:

- This makes both Cocos-frame-end and future SDL3/Flutter-frame-end paths call
  the same runtime presenter pump instead of each path owning a separate swap
  mechanism.

### Source GL texture completion before external present

File:

- `cpp/core/visual/ogl/RenderManager_ogl.cpp`

Change:

- `iTVPRenderManager::PrepareTextureForExternalPresenter(texture)` is now the
  single source-texture handoff point for the Android external presenter.
- The OpenGL manager detaches the active render target from the TVP FBO when the
  external presenter is about to sample that native texture.
- It then calls `glFlush()`.
- It does not call `glFinish()`.

Why:

- A full-frame EGL blit only guarantees the final Android surface submit is
  full-frame. The source GL texture must also have all pending TVP drawing
  commands issued before the presenter samples it.
- `glFlush()` is the light barrier that mirrors the AetherKiri handoff behavior
  without adding hot-path validation or GPU/CPU synchronization stalls.
- This is especially important when `ogl_accurate_render` is OFF: the final
  presenter always draws the full `1920x1080` frame, so stale source texture
  tiles cannot be hidden by a partial final dirty rect.

### Cocos renderer frame-end no longer calls the raw presenter swap

File:

- `platforms/android/cpp/krkr2_android.cpp`

Change:

- `Cocos2dxRenderer.nativeFrameEnd` now calls:
  `TVPRuntimePumpScreenPresenter("cocos-frame-end")`
- It no longer directly calls:
  `TVPSDLAndroidSwapExternalPresenterIfDirty()`

Why:

- Cocos is now only a temporary frame-boundary trigger.
- Final swap ownership is routed through the runtime presenter interface so the
  same path can be used by the no-Cocos SDL3 host.

Expected logs:

- During `BasicDrawDevice::Show`:
  `queued ... path=egl ... fullFrame=1`
- Then:
  `defer-swap sequence=... stage=BasicDrawDevice::Show path=egl`
- At frame boundary:
  `swap-android-egl ... stage=cocos-frame-end ...`
- Then:
  `posted record=... sequence=... fullFrame=1`

## No-Cocos SDL host slice

Files:

- `platforms/android/app/java/org/github/krkr2/AndroidRuntimeBridge.kt`
- `platforms/android/app/java/org/github/krkr2/SdlRuntimeActivity.kt`
- `platforms/android/app/AndroidManifest.xml`
- `platforms/android/app/java/org/github/krkr2/LauncherPrefs.kt`
- `platforms/android/app/java/org/github/krkr2/LauncherActivity.kt`
- `platforms/android/app/java/org/github/krkr2/LauncherSettingsActivity.kt`
- `platforms/android/app/java/org/github/krkr2/ForceLandscapeHelper.kt`
- `.github/workflows/build-android.yml`
- `platforms/android/cpp/krkr2_android.cpp`

Added bridge capabilities:

- `AndroidRuntimeBridge.setApplicationContext(context)`
- `AndroidRuntimeBridge.setGameSurface(surface, width, height)`
- `AndroidRuntimeBridge.resizeGameSurface(width, height)`
- `AndroidRuntimeBridge.detachGameSurface()`
- `AndroidRuntimeBridge.getGameSurfaceMetrics()`
- `AndroidRuntimeBridge.touchBegin/touchMove/touchEnd/touchCancel`

Added activity:

- `SdlRuntimeActivity`

What it does:

- Extends plain `Activity`, not `KR2Activity`.
- Owns a `SurfaceView` with fixed buffer size `1920x1080`.
- Measures the `SurfaceView` as a fixed 16:9 aspect surface centered on a
  black root, so the native game buffer is not stretched on non-16:9 devices.
- Sends that surface to native through `AndroidRuntimeBridge`.
- Starts the game through `AndroidRuntimeBridge.startGame`.
- Pumps frames through `Choreographer`:
  - `AndroidRuntimeBridge.runFrame(deltaSeconds)`
- Sends touch input to the SDL/native input queue.

Native no-Cocos EGL bootstrap:

- `platforms/android/cpp/krkr2_android.cpp` now creates an Android SDL3 render
  EGL context for `TVPAndroidSDLRuntimeHost`.
- It uses an EGL pbuffer as the persistent current surface for engine rendering
  and resource creation.
- The Android external presenter then temporarily switches the same context to
  the fixed `1920x1080` window surface, draws the full-frame quad, marks dirty,
  and restores the pbuffer context.
- This mirrors the reference architecture:
  `pbuffer/current engine context -> full-frame window-surface blit -> dirty
  frame-end swap`.
- The SDL runtime Java pump calls only `runFrame`; native
  `TVPAndroidSDLRuntimeHost::RunFrame()` performs recycle and the single
  frame-boundary presenter pump.

Expected new logs:

- `runtime-host: android-sdl3 EGL render context ready ...`
- `runtime-host: android-sdl3 EGL current ...`
- `android-egl-presenter: queue-android-egl ... fullFrame=1`
- `android-egl-presenter: swap-android-egl ...`

Current status:

- The new SDL runtime activity is behind a settings toggle:
  `SDL3 runtime host`.
- The same toggle is now exposed through the Flutter launcher settings as
  `SDL3 运行时`.
- The Flutter GitHub Actions shell now copies `SdlRuntimeActivity.kt`, declares
  it in the generated manifest, and `LauncherHostActivity` can launch it when
  the toggle is enabled.
- Default remains the legacy `MainActivity : KR2Activity : Cocos2dxActivity`.
- The new path is a migration/audit path and may still need missing startup,
  IME, audio lifecycle, dialog, and video overlay work before it can replace
  the default.

## Risks and next steps

Known risks:

- The no-Cocos activity may reveal missing native bootstrap work previously
  performed by Cocos `AppDelegate`.
- The no-Cocos activity currently uses an Android `SurfaceView` for the native
  game surface. It is a concrete Cocos-removal cut, but the final target should
  still move presentation to Flutter-owned surface/texture ownership.
- Keyboard/IME and menu overlay are not complete in `SdlRuntimeActivity`.

Next steps:

1. Verify logs show `defer-swap` followed by exactly one frame-boundary
   `swap-android-egl`.
2. Test the default Cocos-host path first.
3. Toggle `SDL3 runtime host` only as an audit path.
4. Move Flutter game overlay channel implementation out of `MainActivity` so
   `SdlRuntimeActivity` can own the Flutter + SDL3 path.
5. Add no-Cocos CMake/Gradle audit build with
   `-DKRKR2_ENABLE_COCOS_HOST=OFF` after the SDL runtime activity starts and
   presents reliably.
