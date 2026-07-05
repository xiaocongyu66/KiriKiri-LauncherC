# 2026-07-05 Aether-style deferred EGL sync fix

## User-visible problem

The latest Android OpenGL pipeline regressed into visible synchronization
artifacts:

- With `ogl_accurate_render` disabled, some games show mixed old/new frames.
  User described left side as new and right side as old in one game, and bottom
  old/top new with the top shifted beyond the screen in another game.
- Some OpenGL text-advance scenes still drop to very low FPS, while the software
  renderer does not show the same behavior.
- The game/native output contract must remain fixed at `1920x1080`.
- The hard architecture target is still Flutter + SDL3, with Cocos2dx removed
  from the final render path over time.
- The requested direction is not to stitch random pieces together. Preserve the
  complete AetherKiri-style frame lifecycle: render a full frame into the EGL
  WindowSurface, mark it dirty, and swap only once at frame end when a new frame
  exists.

## Reference rule to preserve

AetherKiri Android render pipeline:

1. `HostWindowLayer::UpdateDrawBuffer()` renders the final composited texture to
   the Android EGL WindowSurface.
2. It clears the full framebuffer, applies the aspect viewport, draws the full
   source texture, and does not use visible partial present.
3. It calls `glFlush()` and marks the EGL frame dirty.
4. `TVPForceSwapBuffer()` consumes the dirty flag and calls `eglSwapBuffers()`.
5. If no new frame was rendered, it does not call `eglSwapBuffers()`.

Important details from AetherKiri:

- Android WindowSurface/SurfaceTexture path does not need Y flip.
- IOSurface is the path that needs Y flip.
- Dirty rects are upload/trigger hints only. The visible present is full-frame
  deterministic.
- `eglSwapBuffers()` is the delivery point to the SurfaceTexture. Do not count a
  frame as posted to Flutter before swap success.

krkrsdl3 reference rule:

- Every present redraws a deterministic full backbuffer:
  `BaseSet(clear + viewport) -> DrawTexture(full source) -> SwapWindow`.

## What was wrong in the current LauncherC pipeline

The current `SDLAndroidFlutterPresenter.cpp` had drifted away from the
Aether-style lifecycle:

- `TryPresentAndroidEGLSurfaceTexture()` rendered into the Flutter
  SurfaceTexture EGL WindowSurface and immediately called `eglSwapBuffers()`.
- That means the frame could become visible during `UpdateDrawBuffer()`, before
  the Cocos/engine frame end boundary.
- In transitional Cocos hosting, multiple `UpdateDrawBuffer()`/dirty updates can
  happen inside one Java `onDrawFrame()`. Immediate swap can expose an
  intermediate backbuffer state and looks exactly like mixed old/new frame
  regions.
- The dirty rect was also consumed in `TVPSDLTryPresentTexture()` as soon as the
  EGL present function returned. In the proper deferred model, the dirty rect
  should only be consumed after the final `eglSwapBuffers()` succeeds.
- `TVPSDLPresentHostWindowTexture()` fed the fixed external presenter viewport
  back into the legacy DrawDevice (`SetDestRectangle`, `SetClipRectangle`,
  `SetViewport`, `SetWindowSize`). In the Cocos transition layer this can disturb
  dirty invalidation coordinates. The external presenter already owns final
  aspect/letterbox drawing, so the DrawDevice should not be reconfigured for the
  fixed Flutter surface during takeover.

## Files changed in this pass

Only the active project was edited:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`
- `cpp/core/environ/sdl/SDLPresentTypes.h`

Reference projects were not edited.

## Code changes

### 1. Android EGL present is deferred again

`TryPresentAndroidEGLSurfaceTexture()` now does:

1. Acquire the fixed Flutter game `ANativeWindow`.
2. Ensure/recreate the EGL WindowSurface at fixed `1920x1080`.
3. Make the external EGL surface current.
4. Set `eglSwapInterval(display, 0)` once per external surface, matching
   AetherKiri's "host controls frame pacing" rule.
5. Use native GL texture if available; software upload remains fallback.
6. Full clear of the `1920x1080` framebuffer.
7. Compute aspect viewport from source texture size to fixed output.
8. Draw the full source texture quad.
9. Run diagnostics `glGetError()` only when the diagnostics env flag is enabled.
10. `glFlush()`.
11. Restore the previous EGL context/surface.
12. Mark `state.frameDirty = true`.
13. Store pending metadata:
    - `pendingNativeGL`
    - `pendingWidth = 1920`
    - `pendingHeight = 1080`
    - `pendingDirtyTexture = texture`
14. Return success as "queued for frame-end swap".

It no longer calls `eglSwapBuffers()` from inside
`TryPresentAndroidEGLSurfaceTexture()`.

### 2. Frame-end swap remains the only post point

`SwapAndroidEGLSurfacePresenterIfDirty()` remains the final post function:

- If `state.frameDirty` is false, it returns false and no swap happens.
- If dirty, it makes the external EGL surface current and calls
  `eglSwapBuffers()`.
- On swap success, it:
  - clears the dirty flag,
  - increments `state.presented`,
  - records `state.lastPresentNativeGL`,
  - increments native-present count when applicable,
  - marks `surfaceHasContent`,
  - remembers the fixed presented surface size,
  - consumes the pending texture dirty rect,
  - marks an external frame as posted.

Java `Cocos2dxRenderer.renderFrame()` already calls native frame end:

```java
nativeRender();
nativeFrameEnd();
```

`nativeFrameEnd()` calls `TVPSDLAndroidSwapExternalPresenterIfDirty()`, so the
actual SurfaceTexture swap is now aligned with the Java/engine frame boundary.

### 3. Dirty rect consumption waits for swap success

`TVPSDLTexturePresentResult` now has:

```cpp
bool deferredSwap = false;
```

The Android EGL plan sets `deferredSwap = true`. Then
`TVPSDLTryPresentTexture()` skips immediate `texture->ConsumeDirtyRect()` for
that path. The dirty rect is consumed only inside
`SwapAndroidEGLSurfacePresenterIfDirty()` after `eglSwapBuffers()` succeeds.

This keeps retry behavior correct if the external surface is recreated or swap
fails.

### 4. DrawDevice viewport feedback is skipped during Android takeover

`TVPSDLPresentHostWindowTexture()` now returns immediately after a successful
present when Android screen takeover is enabled.

Reason:

- The external EGL presenter already performs full-frame clear and aspect
  viewport into fixed `1920x1080`.
- Feeding that fixed output viewport back into the legacy DrawDevice during the
  Cocos transition can scale/offset dirty invalidation coordinates.
- This is especially risky when `ogl_accurate_render` is disabled, because
  partial dirty updates become visible as old/new frame regions.

Keep in mind:

- This is a transitional compatibility choice while Cocos is still hosting the
  loop.
- In the final Flutter + SDL3 host, input/viewport mapping should live in the
  new runtime presenter rather than mutating the old DrawDevice from the final
  present path.

## Expected effect

- No more intermediate SurfaceTexture swaps from inside `UpdateDrawBuffer()`.
- No visible partial-buffer present. The final display remains full clear + full
  textured quad.
- No dirty rect consumption before the Flutter SurfaceTexture has actually been
  posted.
- Less chance of dirty invalidation coordinate corruption when
  `ogl_accurate_render` is disabled.
- The output buffer remains fixed at `1920x1080`.

## Verification done locally

Local toolchain is still missing in this container/session:

- `cmake` not found
- `ninja` not found
- `java` not found
- `flutter` not found
- `dart` not found

Ran:

```sh
git diff --check
```

Result: clean.

## Important next checks

After CI/Android artifact is available, test these cases first:

1. OpenGL pipeline with `ogl_accurate_render` disabled:
   - verify no split old/new frame regions,
   - verify no top/bottom duplicated image,
   - verify no left/right stale image.
2. OpenGL pipeline with text skip/advance:
   - check FPS against software renderer,
   - if still low, inspect whether native GL path is falling back to software
     upload or CPU readback.
3. OpenGL precise render enabled:
   - verify it still behaves as before.
4. Surface recreate/resize:
   - detach/attach Flutter SurfaceTexture,
   - rotate or leave/return activity,
   - verify first frame after recreate is full-frame and fixed `1920x1080`.

## Do not regress these hard requirements

- Do not reintroduce visible partial present.
- Do not swap the external EGL WindowSurface from inside
  `TryPresentAndroidEGLSurfaceTexture()`.
- Do not consume texture dirty rect before deferred `eglSwapBuffers()` succeeds.
- Do not use the Cocos surface as the final visible render target.
- Keep the game/native output fixed at `1920x1080`.
- Continue migrating toward Flutter + SDL3 and away from Cocos2dx.
- Prefer reference-project proven behavior over new speculative validation or
  damage-rect experiments.
