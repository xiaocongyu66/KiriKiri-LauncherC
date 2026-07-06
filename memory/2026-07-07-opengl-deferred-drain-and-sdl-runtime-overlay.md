# 2026-07-07 OpenGL deferred drain / SDL runtime overlay checkpoint

## Hard target

- Main project only: `/root/kiriki-work/KiriKiri-LauncherC`.
- Reference-only projects: `AetherKiri`, `AetherKiri-main`, `krkrsdl2-main`,
  `krkrsdl3-main`, `SDL-release-3.4.10`, and `/root/kiriki-work/docs`.
- Continue migration to Flutter + SDL3.
- Cocos2d-x is compatibility only; keep removing it from launch, frame
  ownership, UI, and final presentation.
- Game/native frame buffer is fixed `1920x1080`. Do not resize it to physical
  display size, Flutter logical size, or Cocos scene size.
- Final visible present must be deterministic full-frame opaque output. Dirty
  rectangles are only internal upload/cache/new-frame hints.
- Do not add hot-path pixel validation, checksums, `glReadPixels`, or graphics
  integrity checks.

## User evidence

User compared screenshots:

- OpenGL bad:
  - `/root/log/Screenshot_2026-07-06-04-32-55-14_5043e5db7c0b5eead9deaa11a1c72650.jpg`
  - `/root/log/Screenshot_2026-07-06-04-33-13-36_5043e5db7c0b5eead9deaa11a1c72650.jpg`
- Software good:
  - `/root/log/Screenshot_2026-07-06-04-36-28-51_5043e5db7c0b5eead9deaa11a1c72650.jpg`
  - `/root/log/Screenshot_2026-07-06-04-33-35-41_5043e5db7c0b5eead9deaa11a1c72650.jpg`

Visual interpretation:

- Software path shows full colorful background, character, UI, and text.
- OpenGL path can show missing background/black content or split stale content.
- Overlay says source/destination are already `0,0,1920x1080`, so the current
  failure is not a wrong final size. It is frame synchronization / stale
  backbuffer presentation.

Latest logs inspected:

- `/root/log/20260707022007423.log`
- `/root/log/78.log`
- `/root/log/sdl3.log`

Important pattern from `20260707022007423.log`:

- EGL presenter creates fixed output correctly:
  `surface ready ... size=1920x1080 fullFrame=1`
- Each `queue-android-egl` is full-frame:
  `rect=0,0,1920x1080 ... nativeGL=5 ... fullFrame=1`
- But swap is delayed until `main-scene-update`:
  `swap-android-egl ... stage=main-scene-update`
- Overwrites appear after startup:
  `sync-overwrite-android-egl #N ... oldDirty=... newDirty=...`
- At `swap-android-egl #2048`, log shows:
  `dirtySerial=2269 swapAttempt=2048 dirtyMarks=2269 dirtyOverwrites=221`

Meaning:

- Full-frame content is drawn, but many dirty full-frame EGL backbuffers are
  queued before a swap drains them.
- The presenter has a single pending dirty slot; later frames overwrite earlier
  frame state before `eglSwapBuffers`.
- This matches user-visible "only newest area is correct; other regions are old"
  and "multiple old/new frames" reports.

## Reference rule

AetherKiri stable rule:

1. `UpdateDrawBuffer()` renders a complete frame into the Android window
   surface / host target.
2. It clears the whole target, draws one aspect-correct full-frame quad, forces
   final alpha to opaque, and marks frame dirty.
3. `TVPForceSwapBuffer()` swaps only if that dirty flag is set.
4. No new frame means no swap, avoiding double-buffer flicker/stale buffer
   alternation.

Relevant references:

- `/root/kiriki-work/AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp`
  - `HostWindowLayer::UpdateDrawBuffer`
  - Clear whole target, aspect viewport, disable scissor/blend/depth, draw full
    quad, flush, mark dirty.
- `/root/kiriki-work/AetherKiri/cpp/core/environ/android/AndroidUtils.cpp`
  - `TVPForceSwapBuffer()` consumes frame dirty before `eglSwapBuffers`.
- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl.cpp`
  - `SDL_AppIterate()` renders app, draws current texture/overlays, then swaps
    exactly once.
- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`
  - GL base setup clears default FBO, disables depth/blend, and draws full
    texture with aspect handling.

## Current code review

The old partial dirty completion is not the remaining root cause:

- `cpp/core/visual/LayerIntf.cpp`
  - Android non-accurate OpenGL completion forces full local layer rect.
- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Android GL-backed takeover presents are expanded to full texture before
    handoff.
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - EGL path clears full output and draws full texture into fixed `1920x1080`.

The remaining issue is deferred swap timing:

- `TryPresentAndroidEGLSurfaceTexture()` draws the full frame and sets
  `state.frameDirty = true`.
- Actual `eglSwapBuffers()` happened later through
  `TVPRuntimePumpScreenPresenter("main-scene-update")` or
  `TVPForceSwapBuffer()`.
- Multiple `BasicDrawDevice::Show` calls can occur before that drain, producing
  `sync-overwrite-android-egl`.

## Change now in working tree

File:

- `cpp/core/environ/sdl/SDLGameManager.cpp`

Change:

- After an Android EGL deferred present succeeds and after pending external
  frame info is stored, immediately call:

```cpp
TVPSDLAndroidFlutterPresenterSwapIfDirty(
    stage ? stage : "android-egl-present");
```

- If this returns true, call:

```cpp
TVPSDLRecordExternalPresenterPostedFrame();
```

- `defer-swap` diagnostics now include `drained=%d`.

Expected new healthy log:

- `queue-android-egl ... stage=BasicDrawDevice::Show ... fullFrame=1`
- `swap-android-egl ... stage=BasicDrawDevice::Show ... dirtyMarks=N dirtyOverwrites=0`
- `posted record=... predicted=...`
- `defer-swap ... drained=1`

Important interpretation:

- Low-level EGL helper still follows Aether's "draw full target, mark dirty"
  rule.
- Runtime presenter now drains that dirty frame at the same
  `BasicDrawDevice::Show` boundary instead of waiting for Cocos/Java frame-end.
- Frame-end / `main-scene-update` remains only a fallback if immediate drain
  fails or no surface is available.

## Android SDL runtime overlay changes currently staged in working tree

Files:

- `platforms/android/app/java/org/github/krkr2/AndroidRuntimeBridge.kt`
- `platforms/android/app/java/org/github/krkr2/SdlRuntimeActivity.kt`
- `platforms/android/cpp/krkr2_android.cpp`

Purpose:

- Continue no-Cocos / Flutter + SDL3 migration.
- Add a plain Android `SdlRuntimeActivity` with:
  - fixed `1920x1080` game surface contract;
  - Flutter overlay engine route `/game-overlay`;
  - MethodChannel for fixed game surface texture creation/resizing/disposal;
  - loading console and FPS/render overlay snapshots;
  - touch/key/lifecycle bridge to native SDL runtime.
- Add `AndroidRuntimeBridge` JNI helpers for loading console and render overlay
  stats.
- On SDL runtime game start, enable screen takeover with fixed `1920x1080`.

SurfaceProducer policy:

- `SdlRuntimeActivity` defaults to legacy `SurfaceTexture` unless
  `KRKR2_ENABLE_FLUTTER_SURFACE_PRODUCER=1/true`.
- This preserves the earlier stability decision after SurfaceFlinger
  out-of-order evidence.

## Verification done locally

Passed:

```sh
git diff --check
```

Toolchain state:

- `java` exists locally.
- `cmake` is not on `PATH`.
- Android SDK is not installed/configured locally.

Attempted:

```sh
/bin/sh ./gradlew :app:compileDebugKotlin :app:compileDebugJavaWithJavac
```

from `platforms/android`.

Blocked by:

```text
SDK location not found. Define a valid SDK location with an ANDROID_HOME
environment variable or by setting sdk.dir in local.properties.
```

## Next test focus

After pushing/building, ask for logs with render diagnostics enabled and check:

- `defer-swap ... drained=1` appears.
- `swap-android-egl` stage should become `BasicDrawDevice::Show` for normal EGL
  frames.
- `dirtyOverwrites` should stop climbing during normal text/background changes.
- `posted-missing-info` should not appear.
- OpenGL non-accurate screenshots should no longer show split old/new frames.

If `drained=0` appears repeatedly:

- Inspect `eglMakeCurrent(swap)` or `eglSwapBuffers` failures.
- Check surface lifecycle: `surface dropped`, `surface ready`, and Flutter
  texture recreate events.
- Do not add pixel readback validation; fix frame ownership/surface lifecycle.

