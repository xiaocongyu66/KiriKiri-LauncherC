# 2026-07-05 OpenGL/EGL Present Sync Fix

## User-reported regression

- Latest logs: `/root/log/20260705093810332.log`,
  `/root/log/20260705093925556.log`,
  `/root/log/20260705094001270.log`,
  `/root/log/20260705094018960.log`,
  `/root/log/20260705094110089.log`,
  `/root/log/20260705094152092.log`,
  `/root/log/20260705094250936.log`,
  `/root/log/20260705094323025.log`,
  `/root/log/20260705094358893.log`, `/root/log/78.log`.
- Symptom: OpenGL pipeline regressed after the previous presenter changes.
- When `ogl_accurate_render` is off, some games show multiple old frames stacked
  vertically or horizontally: new frame on one side, stale frame on the other,
  or new upper region with old lower region.
- Some OpenGL games drop to only a few FPS when advancing text; switching to
  software rendering avoids the issue.

## Hard requirements to preserve

- Continue migration toward Flutter + SDL3.
- Remove Cocos2dx from the final game render path.
- Native game output buffer is fixed `1920x1080`; Flutter may visually contain
  or center it, but native game rendering must not chase the phone screen size.
- Do not add hot-path graphical validation, checksums, or `glReadPixels`.
- Prefer complete render-chain fixes copied from stable reference behavior:
  AetherKiri "swap only when a new frame exists" plus krkrsdl3 deterministic
  full-frame present. Do not combine unrelated partial ideas without wiring the
  whole chain.

## Root cause found in our current chain

The previous Android EGL presenter drew into the Flutter `SurfaceTexture`
window surface, restored the old EGL context, released the window, and only
later swapped the external EGL surface from `TVPForceSwapBuffer()` or Java
`nativeFrameEnd()`.

That deferred swap is unsafe in the current Cocos/GLSurfaceView bootstrap:

1. `TryPresentAndroidEGLSurfaceTexture()` made the external surface current.
2. It drew and `glFlush()`ed the full-screen blit.
3. It restored the previous Cocos EGL surface/context before `eglSwapBuffers`.
4. It set `state.frameDirty = true`.
5. A later frame-end callback rebound the external surface only to swap.

On some Android drivers, the external window backbuffer contents are not a
stable contract after unbinding/restoring away from that EGLSurface before the
swap. That matches the user's old/new half-frame symptoms.

There was also a performance risk: forcing `eglSwapInterval(..., 0)` on a
Flutter `SurfaceTexture` producer can overrun the consumer and make text
advance block on buffer queue backpressure. The reference projects do not need
that forced interval for correctness.

## Implemented fix

Files changed:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

Presenter rule now:

1. If there is no dirty/new frame, do not present.
2. If there is a dirty/new frame, draw a deterministic full-frame blit to the
   fixed `1920x1080` external EGL window surface.
3. Bind framebuffer `0` after making the external surface current, so the blit
   targets the actual window backbuffer rather than any stale engine FBO.
4. Clear the whole `1920x1080` target, then draw the whole texture through the
   aspect viewport.
5. Call `eglSwapBuffers()` immediately before restoring the old EGL context.
6. Only after successful swap:
   - increment EGL presenter `presented`;
   - mark `surfaceHasContent`;
   - remember presented surface size as `1920x1080`;
   - mark external presenter posted frame;
   - return success to the SDL runtime presenter.

`SDLGameManager.cpp` now treats Android EGL as an already-posted present:

- increments `presentedFrames` immediately;
- records `TVPRuntimePresentFrameInfo` immediately;
- consumes the dirty rect immediately;
- clears any old deferred `pendingExternalFrameInfo`.

`SwapAndroidEGLSurfacePresenterIfDirty()` remains as compatibility fallback for
old/deferred state, but the normal EGL path should not depend on it anymore.

## Why this is closer to the references

- AetherKiri's stable rule is "only swap when a real new frame was rendered."
  We keep that gate at `TVPSDLTryPresentTexture()` via dirty rect /
  `frameProduced` handling.
- krkrsdl3's stable rule is deterministic full-frame present. We keep full
  target clear + full-frame blit for the Android EGL `SurfaceTexture`.
- Unlike the broken hybrid, the draw and swap are now one atomic presenter
  operation. The external backbuffer is never drawn, unbound, and then swapped
  later by a different frame-end bridge.

## Notes for the next continuation

- If the user still sees low FPS in OpenGL text advance, inspect producer /
  consumer pacing next. Do not reintroduce `eglSwapInterval(0)` by default.
- If the user still sees old-frame blocks, inspect whether any path bypasses
  `TryPresentAndroidEGLSurfaceTexture()` and still relies on deferred
  `state.frameDirty`.
- The fixed `1920x1080` game surface contract remains current.
- Cocos is still only a bootstrap host at this point; final goal is still to
  move the native game loop and render ownership to Flutter + SDL3 and remove
  Cocos from the game render path.
