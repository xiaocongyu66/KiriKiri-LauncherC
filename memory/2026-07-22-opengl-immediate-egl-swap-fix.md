# 2026-07-22 OpenGL immediate EGL swap fix

## Problem

User reports OpenGL pipeline is broken on game `78` (and generally):

- Software rendering is the correct baseline (full AVG scene).
- OpenGL pipeline produces black / missing content (or stale half-frames).
- OpenGL text skip is slow; large image present is relatively fast.

Screenshots (corrected assignment):

- Software good: full character + dialog.
- OpenGL bad: black / incomplete present.

## Root cause in current code

Android EGL SurfaceTexture present was still deferred incorrectly:

1. `TryPresentAndroidEGLSurfaceTexture()` made the external `1920x1080`
   Flutter window surface current.
2. It full-cleared and drew the game texture.
3. It **restored** the engine EGL surface/context.
4. It only marked `frameDirty = true`.
5. Actual `eglSwapBuffers()` was expected later via pump / frame-end.

Two bugs compounded:

1. **Unbind-before-swap is unsafe** on many Android SurfaceTexture producers.
   After restoring away from the external EGLSurface, the backbuffer is not a
   stable contract; later rebind+swap can post black or stale contents.
2. **Immediate drain was documented but not wired** in
   `TVPSDLTryPresentTexture()`: `deferredSwapDrained` stayed `false` and
   `TVPSDLAndroidFlutterPresenterSwapIfDirty()` was never called at the
   `BasicDrawDevice::Show` boundary. Multiple full-frame blits overwrote one
   pending dirty slot (`sync-overwrite-android-egl`).

Memory notes already described both fixes
(`2026-07-05-immediate-egl-surface-swap-after-1845-logs.md`,
`2026-07-06-frame-synchronous-deferred-egl-drain.md`) but the working tree had
regressed to the deferred/unsafe path (`result.deferredSwap = true` and no
Show-boundary drain).

## Fix (KiriKiri-LauncherC only)

### 1. Immediate swap while external surface is current

File: `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

`TryPresentAndroidEGLSurfaceTexture()` now:

1. Make external Flutter EGL window surface current.
2. Full clear opaque black + draw full source texture (aspect viewport).
3. `glFlush()`.
4. **`eglSwapBuffers()` immediately while still current**.
5. Restore previous engine EGL current state.
6. Clear any deferred dirty flag so later frame-end drains no-op.
7. Mark external presenter posted + consume texture dirty rect.

`TryPresentAndroidTexturePlan()` sets `result.deferredSwap = false` so the
runtime records an immediate present.

Expected log:

```text
present-android-egl #N ... fullFrame=1 immediateSwap=1
```

instead of only:

```text
queue-android-egl ...
sync-overwrite-android-egl ...
swap-android-egl ... stage=main-scene-update
```

### 2. Safety drain for residual deferred path

File: `cpp/core/environ/sdl/SDLGameManager.cpp`

If any path still returns `deferredSwap=true`, drain at the same Show boundary:

```cpp
deferredSwapDrained = TVPSDLAndroidFlutterPresenterSwapIfDirty(stage);
if(deferredSwapDrained)
    TVPSDLRecordExternalPresenterPostedFrame();
```

## Constraints preserved

- Fixed native game surface `1920x1080`.
- Flutter overlay remains menu/input only.
- No hot-path `glReadPixels` / integrity checks.
- Only `KiriKiri-LauncherC` modified.

## Validation

Local Android SDK / NDK / Java are not fully available in this container for a
full APK build. Verify on device:

```sh
./platforms/android/gradlew -p ./platforms/android assembleDebug
```

Then run game `78` with OpenGL pipeline and confirm:

1. Scene matches software baseline (not black).
2. Logs show `present-android-egl ... immediateSwap=1`.
3. `sync-overwrite-android-egl` / late `swap-android-egl stage=main-scene-update`
   should largely disappear for normal Show frames.
4. Text skip should improve if previous stalls were SurfaceTexture producer
   backlog from delayed/overwritten swaps.

Optional diagnostics:

```sh
export KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS=1
```
