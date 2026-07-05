# 2026-07-05 Aether frame-end swap integration

## Scope

Only modify:

- `/root/kiriki-work/KiriKiri-LauncherC`

Reference-only projects:

- `/root/kiriki-work/AetherKiri`
- `/root/kiriki-work/krkrsdl2-main`
- `/root/kiriki-work/krkrsdl3-main`
- `/root/kiriki-work/docs`
- `/root/kiriki-work/SDL-release-3.4.10`

Hard target remains:

`迁移到 Flutter + SDL3，新渲染链路完整、高性能、高兼容，逐步并最终去除 Cocos2dx。Cocos 只能作为临时 bootstrap/compatibility，不是最终 renderer。`

## Why this pass was needed

The previous pass moved Android EGL present closer to AetherKiri by drawing the
external Flutter `ANativeWindow` EGL surface in
`TryPresentAndroidEGLSurfaceTexture()` and deferring the real
`eglSwapBuffers()` until `TVPForceSwapBuffer()`.

Subagent review found an important host mismatch:

- AetherKiri has its own engine frame loop and calls `TVPForceSwapBuffer()` at
  the official frame end.
- KiriKiri-LauncherC still runs through Android `GLSurfaceView.Renderer`.
  Normal frames enter Java `Cocos2dxRenderer.onDrawFrame()` and call
  `nativeRender()`.
- `GLSurfaceView` performs its own Cocos EGL surface swap after `onDrawFrame()`
  returns.
- Therefore, if no explicit frame-end hook calls the external presenter swap,
  the Flutter/SDL external surface can be left with a dirty drawn frame that was
  never posted, while the legacy Cocos surface is still swapped by
  `GLSurfaceView`.

This could explain the user's report after the deferred-swap change:

- left side new frame / right side old frame;
- top new frame / bottom old frame;
- frames appear shifted or stale even though the game resolution is fixed
  1920x1080.

## Reference behavior copied as a whole

### AetherKiri

Reference files:

- `/root/kiriki-work/AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp`
- `/root/kiriki-work/AetherKiri/cpp/core/environ/android/AndroidUtils.cpp`
- `/root/kiriki-work/AetherKiri/cpp/core/visual/ogl/krkr_egl_context.h`

Important behavior:

1. `UpdateDrawBuffer()` really blits a new frame.
2. After the blit completes, it calls `egl.MarkFrameDirty()`.
3. `TVPForceSwapBuffer()` calls `egl.ConsumeFrameDirty()`.
4. If there was no dirty produced frame, it does not swap.
5. If there was a dirty produced frame, it swaps exactly once.

The important part is not only "defer swap"; the important part is "defer to a
real frame-end point that is actually reached every rendered frame."

### krkrsdl3

Reference files:

- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl.cpp`
- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`

Important behavior:

- final visible present is deterministic full-frame:
  - bind default framebuffer;
  - disable depth/blend;
  - clear whole output;
  - draw a complete textured quad into an aspect viewport;
  - swap.
- pitch-aware upload is used for texture upload, not as a visible partial
  present contract.

## Code changes in this pass

### Java frame-end hook

File:

- `platforms/android/libcocos2dx/java/src/org/cocos2dx/lib/Cocos2dxRenderer.java`

Change:

- Added `renderFrame()` helper.
- `renderFrame()` calls:
  1. `nativeRender()`
  2. `nativeFrameEnd()`
- Both branches of `onDrawFrame()` now call `renderFrame()` instead of directly
  calling `nativeRender()`.

Why:

- This gives the external Flutter/SDL presenter a real frame-end hook in the
  current Cocos/GLSurfaceView bootstrap host.
- It is the closest local equivalent of AetherKiri's official engine swap
  point while Cocos still owns the outer Android GL loop.

### Native frame-end hook

File:

- `platforms/android/cpp/krkr2_android.cpp`

Change:

- Added JNI method:

`Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeFrameEnd(...)`

- It calls:

`TVPSDLAndroidSwapExternalPresenterIfDirty()`

Why:

- It swaps only the dirty external presenter frame.
- It does not call the legacy Cocos `eglSwapBuffers()` manually.
- This avoids double-swapping the Cocos surface from inside `onDrawFrame()`,
  because `GLSurfaceView` will still do its own legacy surface swap when the
  renderer callback returns.

### Header export

File:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.h`

Change:

- Declared:

`extern "C" bool TVPSDLAndroidSwapExternalPresenterIfDirty();`

Why:

- Android JNI code can call the external presenter swap hook directly.

### EGL pending dirty ownership

File:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Change:

- `TVPAndroidEGLSurfacePresenterState` now stores:
  - `iTVPTexture2D *pendingDirtyTexture`
- `TryPresentAndroidEGLSurfaceTexture()` stores the texture pointer when it
  draws a pending external EGL frame.
- `SwapAndroidEGLSurfacePresenterIfDirty()` consumes the texture dirty rect
  only after `eglSwapBuffers()` succeeds.
- If swap fails or the surface is missing, the dirty rect is not consumed, so a
  later frame can retry.

Why:

- The previous code consumed texture dirty metadata immediately after drawing
  into the external EGL back buffer, before the buffer was actually posted.
- If swap failed after that, the dirty signal was gone and the frame could be
  lost.
- AetherKiri consumes its produced-frame dirty flag at the actual swap point,
  not before.

### Posted-frame marker after EGL swap

File:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Change:

- Successful `SwapAndroidEGLSurfacePresenterIfDirty()` now calls
  `MarkExternalPresenterPostedFrame()`.

Why:

- If a later legacy `TVPForceSwapBuffer()` runs in the same frame, it can
  consume the posted-frame marker and skip stale/duplicate legacy swap work.

### Surface lifecycle cleanup

File:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Changes:

- Added `ClearRememberedPresentedSurface()`.
- `TVPSDLAndroidFlutterPresenterNotifySurfaceChanged()` clears remembered
  presented size and posted-frame marker.
- `DestroyAndroidEGLWindowSurfaceLocked()` now clears pending frame state:
  - `frameDirty`
  - `pendingNativeGL`
  - `pendingWidth`
  - `pendingHeight`
  - `pendingDirtyTexture`
- `TVPSDLAndroidIsExternalPresenterActive()` no longer treats old
  `presented > 0` or old direct-present counters as permanently active.
  It now reports active only when there is:
  - a dirty pending EGL frame; or
  - a valid EGL surface that has presented; or
  - a remembered currently presented surface size; or
  - a just-posted external frame marker.

Why:

- Old surface dimensions or old `presented > 0` state could survive detach,
  resize, rotation, or surface recreation.
- That could make Flutter reuse stale dimensions or make Android skip legacy
  fallback while no valid external surface exists, producing frozen old frames.

### Produced-frame signal

Files:

- `cpp/core/environ/runtime/RuntimePresenter.h`
- `cpp/core/visual/impl/BasicDrawDevice.cpp`
- `cpp/core/environ/cocos2d/MainScene.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

Changes:

- `TVPRuntimeTexturePresentRequest` gained `frameProduced`.
- `BasicDrawDevice::Show()` sets `frameProduced = true`.
- `WindowLayer::UpdateDrawBuffer()` sets `frameProduced = true`.
- Android native-GL takeover path treats a produced GL-backed frame with no
  dirty rect as a full-frame GPU present trigger.
- This path does not invalidate the pixel cache, avoiding accidental GL
  readback/CPU copy.

Why:

- During the Cocos-to-SDL transition, dirty metadata may be incomplete for
  GL-backed textures even when a new composed GPU frame exists.
- Returning "success" without presenting, or doing nothing because there is no
  dirty rect, can leave the external surface with an old frame.
- AetherKiri's rule is about produced frames, not only dirty rectangles.

Important constraint:

- This full-frame produced-frame trigger is only enabled for Android native GL
  takeover (`skipShadowUploadForNativeAndroid`).
- It is not enabled for software by default because that would force a full CPU
  upload every `Show()` and hurt performance.

## Current chain after this pass

Native GL Android takeover path:

1. Engine/cocos bootstrap calls `BasicDrawDevice::Show()` or
   `WindowLayer::UpdateDrawBuffer()`.
2. The request carries `frameProduced = true`.
3. `TVPSDLTryPresentTexture()` computes dirty metadata if available.
4. If there is no dirty rect but a GL-backed produced frame exists, it forces a
   full-frame GPU present trigger.
5. `TVPSDLAndroidFlutterPresenterTryPresentTexturePlan()` goes into Android EGL.
6. `TryPresentAndroidEGLSurfaceTexture()`:
   - calls `PrepareTextureForExternalPresenter(texture)`;
   - binds the Flutter external EGL surface;
   - clears the entire output;
   - computes aspect viewport;
   - draws a complete texture quad;
   - marks `frameDirty`;
   - stores pending texture dirty ownership;
   - restores the previous EGL current surface/context.
7. Java `Cocos2dxRenderer.onDrawFrame()` reaches `nativeFrameEnd()`.
8. `nativeFrameEnd()` calls `TVPSDLAndroidSwapExternalPresenterIfDirty()`.
9. The swap hook consumes exactly one dirty external frame and calls
   `eglSwapBuffers()` on the Flutter EGL surface.
10. After swap success, it consumes the texture dirty rect and marks an
    external frame as posted.
11. If a legacy explicit `TVPForceSwapBuffer()` also runs, it sees the posted
    external frame and skips stale legacy swap work.

## Do not regress these rules

- Do not re-enable partial EGL visible present by default.
- Do not reintroduce preserved-swap assumptions.
- Do not make dirty rect control the visible present rectangle.
- Do not default GL-backed frames to CPU fallback or `glReadPixels`.
- Do not put raw `extern void TVPSetRenderTarget(GLuint)` back into presenter
  code; keep the detach in `PrepareTextureForExternalPresenter()`.
- Do not add hot-path checksums, graphical integrity validation, or
  `glReadPixels` validation.
- Do not treat Cocos as final architecture. The frame-end hook is a temporary
  bridge while moving to Flutter + SDL3 ownership.

## Verification

Local verification:

- `git diff --check` passed.

Not run locally:

- Android/Gradle/Flutter build, because this environment still lacks local
  build tools such as Java, CMake, Ninja, Clang, Flutter, and Dart.

## Next steps

1. Build on GitHub Actions and test on device.
2. Watch `78.log` for:
   - `android-egl-presenter` surface ready/dropped messages;
   - `swap-android-egl` messages when diagnostics are enabled;
   - old/new split frames after this frame-end hook.
3. If split frames continue, inspect whether the visible Flutter game surface
   and legacy GLSurfaceView are both being composed at the same z-order.
4. Continue removing Cocos ownership:
   - Android lifecycle/input from `Cocos2dxActivity`;
   - `MainScene`/`WindowLayer` as game presenter;
   - visual/movie unconditional Cocos dependencies;
   - Cocos Studio UI paths.

## Same-pass refinement after subagent review

Subagents identified another semantic mismatch:

- `TVPSDLTryPresentTexture()` was incrementing SDL presenter `presentedFrames`
  as soon as the EGL presenter accepted a plan.
- At that point the frame had only been drawn into the external EGL back
  buffer; it had not necessarily been posted with `eglSwapBuffers()`.
- `MainScene::update()` hides Cocos nodes when
  `TVPRuntimeHasPresentedFrame()` becomes true, so counting the frame too early
  could hide the fallback surface before the external surface was actually
  visible.
- `TryPresentAndroidEGLSurfaceTexture()` was also remembering presented surface
  size before the external EGL swap. That could feed an unposted/stale size
  back into Flutter.

Additional changes:

- `SwapAndroidEGLSurfacePresenterIfDirty()` now returns true only when an
  external EGL frame was actually swapped successfully.
- Android `TVPForceSwapBuffer()` calls
  `TVPSDLRecordExternalPresenterPostedFrame()` only after that successful
  external swap.
- Java `nativeFrameEnd()` also records the external presenter frame only when
  `TVPSDLAndroidSwapExternalPresenterIfDirty()` succeeds.
- `TVPSDLTryPresentTexture()` stores EGL path `TVPRuntimePresentFrameInfo` as
  pending instead of immediately incrementing `presentedFrames`.
- `TVPSDLRecordExternalPresenterPostedFrame()` increments `presentedFrames`,
  fills the pending frame sequence, and calls `TVPRuntimeRecordPresentFrame()`
  at real post time.
- Direct `ANativeWindow_unlockAndPost()` fallback still records immediately,
  because that path posts synchronously before returning.
- The EGL draw stage no longer calls
  `TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize()`. Presented
  size is remembered only after the successful external swap.
- `TVPSDLPresentHostWindowTexture()` uses the requested Flutter game surface
  size as the Android takeover viewport authority, and only falls back to
  remembered presented size when the requested size is unavailable.
- `TVPSDLNotifyAndroidFlutterGameSurfaceChanged()` clears
  `presentedFrames` and pending external frame info, so resize/detach/set does
  not let old posted state drive the next frame.

This makes the local chain stricter:

`plan accepted != frame posted`

Only `eglSwapBuffers()` success or synchronous `ANativeWindow_unlockAndPost()`
success counts as a presented frame.
