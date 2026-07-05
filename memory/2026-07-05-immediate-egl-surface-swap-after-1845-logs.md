# 2026-07-05 Immediate EGL SurfaceTexture swap after 18:45 logs

## Context

The user provided the 18:45 Android logs:

- `/root/log/20260705184514016.log`
- `/root/log/20260705184544539.log`
- `/root/log/78.log`

Those logs showed:

- SDL takeover enabled with `surfaceMirror=0`.
- The Flutter game `ANativeWindow` and Android EGL presenter surface were
  created successfully at fixed `1920x1080`.
- The EGL blit program was created successfully.
- There were no visible `present-android-egl`, `queue-android-egl`, or
  `swap-android-egl` success lines because the success logs are diagnostics
  gated.

The suspicious part was the current deferred presenter lifecycle:

1. Draw into the Flutter `SurfaceTexture` EGL surface.
2. `glFlush()`.
3. Restore the previous Cocos/engine EGL surface/context.
4. Mark `frameDirty=true`.
5. Later make the external surface current again just to call
   `eglSwapBuffers()`.

On Android `SurfaceTexture` producers this can leave the backbuffer crossing a
context/surface restore boundary before it is posted.

## Change

Edited only the active project:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

`TryPresentAndroidEGLSurfaceTexture()` now swaps immediately while the external
Flutter EGL window surface is still current:

1. Make the external `EGLSurface` current.
2. Draw the deterministic full-frame blit into fixed `1920x1080`.
3. Call `eglSwapBuffers(state.display, state.surface)` immediately.
4. Restore the previous EGL current surface/context.
5. Only after swap success:
   - increment `state.presented`;
   - update `lastPresentNativeGL` / `nativePresents`;
   - mark `surfaceHasContent`;
   - remember the fixed presented surface size;
   - mark the external presenter posted frame.

The Android EGL plan now sets `result.deferredSwap = false`, so
`SDLGameManager.cpp` consumes the texture dirty rect immediately after the
successful already-posted present.

`SwapAndroidEGLSurfacePresenterIfDirty()` is still kept for compatibility and
will naturally no-op on the normal immediate-swap path because `frameDirty`
stays false.

## Pacing

The old default `eglSwapInterval(display, 0)` call is no longer unconditional.
It is now opt-in via:

```sh
KRKR2_ANDROID_EGL_SWAP_INTERVAL_ZERO=1
```

Defaulting to the consumer-controlled SurfaceTexture pacing is safer for
Flutter and avoids forcing interval-zero producer behavior on Android drivers.

## Verification

Local toolchain is still unavailable in this container:

- `cmake` not found
- `ninja` not found
- `java` not found
- `flutter` not found

Ran:

```sh
git diff --check
```

Result: clean.
