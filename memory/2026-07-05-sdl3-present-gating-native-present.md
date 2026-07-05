# 2026-07-05 SDL3 presenter gating and native GL present-only fix

## Scope

- Work project: `/root/kiriki-work/KiriKiri-LauncherC`.
- Reference-only projects:
  - `/root/kiriki-work/AetherKiri`
  - `/root/kiriki-work/krkrsdl2-main`
  - `/root/kiriki-work/krkrsdl3-main`
  - `/root/kiriki-work/docs`
  - `/root/kiriki-work/SDL-release-3.4.10`
- Do not modify reference projects.
- Hard target remains Flutter + SDL3 migration and gradual removal of Cocos2dx.
- User requirements remain: complete, high-performance, high-compatibility, no
  heavy graphical integrity validation on the render hot path.

## User-visible problem this pass addresses

The latest `78.log` run and user report described the OpenGL pipeline as badly
broken:

- one game showed left side as new frame and right side as old frame;
- another game showed top as new frame and bottom as old frame, with the new
  portion shifted/out of screen;
- fixed `1920x1080` game resolution is expected and supported.

These symptoms are consistent with stale swap-buffer presentation, not with
logical resolution. Two concrete problems existed in the current source:

1. `TVPSDLTryPresentTexture()` had a `nativePresentOnly` branch for GL-backed
   textures with no dirty rect, but it returned `true` immediately without
   presenting anything. This could make frames disappear whenever the hybrid
   Cocos/OpenGL path advanced GPU content without producing dirty metadata.
2. Android `TVPForceSwapBuffer()` always called `eglSwapBuffers()` after the
   frame loop. When the SDL/Flutter external presenter had already posted the
   frame, the follow-up Cocos-era swap could flip to an old back buffer. This
   is the exact failure mode AetherKiri warns about.

## Reference behavior used

### AetherKiri

Relevant file:

- `/root/kiriki-work/AetherKiri/cpp/core/environ/android/AndroidUtils.cpp`

Key idea:

- `TVPForceSwapBuffer()` swaps only when a frame was actually rendered and
  marked dirty.
- Aether's comment explicitly says unconditional swaps in double-buffered mode
  alternate current and stale buffers and produce previous-image flicker.

### krkrsdl3-main

Relevant files:

- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl.cpp`
- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`

Key idea:

- Every iteration clears framebuffer 0, draws full textured quads, then swaps.
- Pitch-aware upload is used for texture data, but present itself is full and
  deterministic. The final swap is not used as an implicit dirty rectangle
  compositor.

## Changes applied

### `cpp/core/environ/sdl/SDLGameManager.cpp`

Fixed `nativePresentOnly`:

- Before:
  - if GL-backed texture had no dirty rect and the presenter had already shown
    one frame, the function returned success without calling Android EGL.
- After:
  - that case builds a full-frame present rect and goes through
    `TVPSDLAndroidFlutterPresenterTryPresentTexturePlan()`.
  - It does not run SDL_GPU shadow upload.
  - It does not invalidate CPU pixel cache.
  - It does not consume texture dirty state because there was no dirty metadata.
  - It still disallows CPU fallback for this path, so a missed native EGL
    present does not silently become a slow `glReadPixels`/CPU copy path.

This makes the hybrid architecture closer to krkrsdl3/Aether behavior: native
GL frames can be re-presented by drawing the existing GPU texture, even when
old Cocos-era dirty metadata is incomplete.

### `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Added a lightweight posted-frame flag:

- `gExternalPresenterPostedFrame`
- `MarkExternalPresenterPostedFrame()`
- exported C symbol:
  - `TVPSDLAndroidConsumeExternalPresenterPostedFrame()`

The flag is set after successful external presentation:

- successful EGL `eglSwapBuffers()` path;
- successful direct `ANativeWindow_unlockAndPost()` texture path;
- successful direct `ANativeWindow_unlockAndPost()` surface path.

This is intentionally not a graphical validation/checksum. It is one atomic
flag used to avoid an immediately following duplicate/stale Cocos-era swap.

### `cpp/core/environ/android/AndroidUtils.cpp`

Changed `TVPForceSwapBuffer()`:

- It first consumes `TVPSDLAndroidConsumeExternalPresenterPostedFrame()` when
  the SDL presenter is linked.
- If an external presenter already posted the current frame, it returns without
  calling `eglSwapBuffers()`.
- Otherwise it performs the legacy swap, but skips invalid
  `EGL_NO_DISPLAY`/`EGL_NO_SURFACE`.

The SDL consume function is declared weak so Android builds that do not link
the SDL presenter can still use the old swap path.

## Why this should improve correctness

- The final Android visible surface is now posted once per produced external
  frame instead of being posted once by the SDL/Flutter presenter and then
  immediately swapped again by the Cocos-era loop.
- GL-backed frames with missing dirty metadata are no longer treated as "success
  but no-op"; they are re-presented as a full GPU quad.
- Dirty rectangles still matter for CPU/software upload boundaries, but native
  GPU presentation no longer depends on dirty metadata being perfect while the
  project is still between Cocos and SDL3.

## Performance impact

- The no-dirty/native-GL path adds one full-screen GPU quad present. That is
  cheap compared with CPU readback or full software upload, and it is exactly
  the path the reference projects use for stable present.
- The posted-frame flag is a single atomic store/exchange per external frame.
- No per-pixel checks, no `glReadPixels`, no checksums, no extra default logs.

## Verification performed locally

- `git diff --check` passed.
- Local build could not be run because this container still lacks:
  - `cmake`
  - `ninja`
  - `clang++`
  - `java`

## Next migration notes

- Continue moving the Android frame owner to SDL3/Flutter and away from Cocos.
- Keep final present full-frame and deterministic.
- Keep dirty rect only at upload/cache boundaries.
- Keep `iTVPRenderManager::PrepareTextureForExternalPresenter()` as the GL
  render-target detach boundary; presenter code should not grow raw extern GL
  hooks again.
- If the next device run still shows visual splits, inspect whether there is
  any remaining path that calls `eglSwapBuffers()` after external presentation
  or swaps a SurfaceTexture without a produced frame.

## Follow-up completed in the same 2026-07-05 continuation

The user clarified that the previous work was still too fragmented. The render
chain must be treated like AetherKiri's whole loop, not as unrelated patches.

New hard rule written to memory:

- `memory/2026-07-05-hard-target-remove-cocos-aether-pipeline.md`
- The final target is Flutter + SDL3.
- Cocos2dx is temporary bootstrap/compatibility only and must be removed from
  the game render path over time.

Additional code changes:

- `iTVPDrawDevice::SetViewport()` was added.
- `tTVPDrawDevice` now stores a viewport rectangle and uses it in input
  coordinate transforms.
- `TVPSDLPresentHostWindowTexture()` now computes an aspect-preserving viewport
  and sets draw-device `DestRect`, `ClipRect`, `Viewport`, and full output size
  together.
- Android EGL partial final-present was disabled. EGL now follows the
  full-frame deterministic present rule.
- Android direct `ANativeWindow` partial final-present was disabled. Dirty rect
  remains useful for upload/cache, but final post is full-frame.
- SDL renderer fallback disables SDL logical stretch, clears the output, and
  draws the full texture into an explicit aspect-preserving viewport.
- SDL window-surface fallback now blits/posts the full surface, not only the
  dirty rectangle.
- `TVPSDLAndroidIsExternalPresenterActive()` was added so Android legacy
  `TVPForceSwapBuffer()` can skip Cocos-era swaps once SDL/Flutter has posted a
  frame.

Important semantic correction:

- The previous "nativePresentOnly" path that presented a GL texture even with
  no dirty/new-frame signal was removed.
- Now no dirty/new frame means no external present, matching the AetherKiri
  "only swap when a frame was produced" rule.
- The first takeover frame and surface-size changes still force a full-frame
  present.

Additional continuation fixes:

- `tTVPBasicDrawDevice::SetDestRectangle()` now always calls the base
  implementation. Before this, the D3D-era body was compiled out, so the SDL
  viewport/dest update could fail to update `DestRect` on BasicDrawDevice.
- Android EGL preserved-swap/partial-present probing was removed from the hot
  path. The EGL presenter now has no partial-present branch.
- Android EGL fullscreen quad UVs were aligned with AetherKiri:
  bottom vertices use `v=1`, top vertices use `v=0`.
- `KRKR2_ANDROID_EGL_SURFACE_FLIP_Y` now defaults to false. Android
  WindowSurface/SurfaceTexture should not receive an extra Y flip by default;
  the env var remains available for emergency override.

## 2026-07-05 continuation: deferred EGL swap gate

The Android EGL path no longer swaps immediately from
`TryPresentAndroidEGLSurfaceTexture()`.

New behavior:

- `TryPresentAndroidEGLSurfaceTexture()` draws the deterministic full frame to
  the Flutter EGL surface and sets `TVPAndroidEGLSurfacePresenterState::frameDirty`.
- `TVPForceSwapBuffer()` calls
  `TVPSDLAndroidSwapExternalPresenterIfDirty()` before any legacy Cocos swap.
- The swap helper consumes exactly one dirty EGL frame, calls
  `eglSwapBuffers()` for the Flutter surface, restores the previous EGL
  context/surface, and records EGL-present counters.
- If there is no dirty external frame, no external EGL swap happens.
- Direct CPU `ANativeWindow_unlockAndPost()` fallback still uses
  `TVPSDLAndroidConsumeExternalPresenterPostedFrame()` to prevent a second
  legacy swap after a posted external frame.

This is the current closest local equivalent of AetherKiri's:

`UpdateDrawBuffer()` / external blit / `MarkFrameDirty()` /
`TVPForceSwapBuffer()` / `ConsumeFrameDirty()` / `eglSwapBuffers()`.

The EGL presenter also now cleans up the small GL state it touches after
drawing the full-screen quad:

- disables its vertex attrib arrays;
- unbinds `GL_ARRAY_BUFFER`;
- unbinds `GL_TEXTURE_2D`;
- resets `glUseProgram(0)`.

That cleanup is intentionally cheap and follows AetherKiri's blit cleanup
style. It avoids leaving the external-present shader/VBO/texture bound for the
next engine draw without adding expensive per-frame state validation.
