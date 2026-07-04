# 2026-07-05 Android EGL full-frame present recovery

## Scope and hard constraints

- Modify only `/root/kiriki-work/KiriKiri-LauncherC`.
- Reference-only projects remain:
  - `/root/kiriki-work/AetherKiri`
  - `/root/kiriki-work/docs`
  - `/root/kiriki-work/kirikiroid2-web`
  - `/root/kiriki-work/KrKr2-Next`
  - `/root/kiriki-work/krkrsdl2-main`
  - `/root/kiriki-work/krkrsdl3-main`
  - `/root/kiriki-work/SDL-release-3.4.10`
- Hard target remains migration toward Flutter + SDL3 and gradual removal of
  the Cocos2dx host/presenter path.
- User requirements remain complete, high-performance, high-compatibility, and
  reference-proven. Avoid hot-path graphical integrity validation, checksums,
  `glReadPixels`, repeated `glGet*`, or noisy logs unless a diagnostic switch
  explicitly enables them.

## Current user-visible symptom

The OpenGL/EGL presenter regressed into stale-buffer artifacts:

- One game shows the left side as the new frame and the right side as an old
  frame.
- Another game shows the top as the new frame and the bottom as an old frame,
  with the new portion visually shifted/out of screen.

This matches partial-swap / preserved-back-buffer style failure: only the dirty
rectangle is redrawn, but the non-dirty part of the EGL back buffer is not
guaranteed to contain the previous complete frame on Android/ANGLE/SurfaceView.
When that preserved content is not actually preserved, the result is a
mixed-frame buffer.

## Log findings from `/root/log/78.log`

Only `/root/log/78.log` existed locally; `20260704105124483.log` was not under
`/root/log`.

Important lines from the updated `78.log`:

- Android selects ANGLE for `org.github.krkr2` and reports Vulkan on
  Mali-G720.
- The SDL runtime is SDL 3.4.10.
- The active host is still Cocos/GLThread in the stack.
- The game `SurfaceView` queueBuffer FPS drops hard in the bad sections.
- Native logging path in the device log was
  `/storage/emulated/0/krkr2pro/logs/20260705042924539.log`.
- `android-egl-presenter` creates/drops EGL surfaces and later uses the
  Flutter game surface present path.
- Earlier crash in the same log had:
  `TVPDeallocateRegionRect -> tTVPComplexRect::Clear ->
  tTVPSoftwareTexture2D_static::MarkDirtyRect`, but current source already has
  the software texture dirty rect reduced to `bool AdapterDirty +
  tTVPRect AdapterDirtyRect` in commit `984943b`.

## Reference behavior copied

### AetherKiri

Relevant reference file:

- `/root/kiriki-work/AetherKiri/cpp/core/environ/android/AndroidUtils.cpp`

Key behavior:

- `TVPForceSwapBuffer()` swaps only when the engine frame is actually dirty.
- The comment explicitly warns that swapping with no new frame in
  double-buffered mode alternates current and stale buffers and produces
  previous-image flicker.
- It does not depend on reusing stale back-buffer contents as the visible
  frame. Presentation is gated by real frame production.

### krkrsdl3-main

Relevant reference files:

- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`
- `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl.cpp`

Key behavior:

- Every iteration calls `SDL_GL_BaseSet()`.
- `SDL_GL_BaseSet()` binds framebuffer 0, disables depth/blend, clears the full
  color buffer, sets viewport, binds VAO/program.
- `SDL_GL_DrawTexture()` draws a complete textured quad.
- `SDL_GL_SwapWindow()` presents the complete frame.
- Texture upload uses pitch-aware `GL_UNPACK_ROW_LENGTH`, but presentation
  itself is full-frame. Dirty/pitch optimization belongs to upload/cache, not
  to assuming old swap-chain contents are valid.

## Patch applied in KiriKiri-LauncherC

Changed files:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

Behavior change:

- Android EGL partial present is now disabled by default.
- New optional override:
  - `KRKR2_ANDROID_EGL_PARTIAL_PRESENT=1`
- `EGL_BUFFER_PRESERVED` is only requested when that override is enabled.
- Scissor/partial draw is only allowed when:
  - `KRKR2_ANDROID_EGL_PARTIAL_PRESENT=1`
  - the EGL config supports preserved swap
  - the surface reports preserved swap behavior
  - the surface already has content
  - the dirty rect is smaller than the surface
- Default behavior is now reference-style full-frame blit:
  - dirty rect still limits software texture upload into the private EGL upload
    texture
  - the final EGL draw clears and redraws the whole surface with one textured
    quad
  - native GL texture presentation remains CPU-copy-free and only pays the
    cheap full-screen quad

## Why this should fix the artifact

The previous path used dirty rect as both upload rect and present rect. That is
unsafe on Android EGL unless preserved swap is truly reliable. The observed
left/right and top/bottom old-new split is exactly what happens when the
non-redrawn part of the back buffer contains stale or unrelated pixels.

The new default separates responsibilities:

- Dirty rect is still used for CPU-to-GPU upload, so software-backed textures do
  not have to upload 1920x1080 every time after the first full upload.
- Presentation no longer trusts non-dirty back-buffer contents. The whole
  Flutter/Android EGL surface is redrawn every presented frame.
- Native GL-backed textures remain fast because full-frame present is a single
  GPU quad and no CPU copy.

Follow-up CPU fallback gate:

- Android GL-backed textures no longer fall back to the CPU Android direct
  presenter or SDL surface mirror by default when EGL presentation fails.
- New optional override:
  - `KRKR2_ANDROID_ALLOW_GL_CPU_FALLBACK=1`
- This avoids the worst fallback path for OpenGL textures:
  `GetScanLineForRead()` can force `glReadPixels` / full texture readback,
  which is not used by the reference full-frame GPU presenter design and can
  destroy performance or amplify stale-frame issues.
- Software-backed textures can still use CPU fallback normally.

This follows the reference projects more closely than the previous partial EGL
present experiment.

## Compatibility notes

- If a future device proves partial EGL present is correct and measurably
  faster, test it explicitly with `KRKR2_ANDROID_EGL_PARTIAL_PRESENT=1`.
- Do not make partial EGL present the default again unless there is strong
  evidence across Android versions, ANGLE/Vulkan, GLES, and device vendors.
- If EGL presentation fails for a GL-backed frame, the default behavior now
  returns to the caller so the legacy form path can decide what to do instead
  of silently forcing a GPU-to-CPU readback. Use
  `KRKR2_ANDROID_ALLOW_GL_CPU_FALLBACK=1` only for diagnosis.
- The remaining Cocos GLThread is still present. The long-term fix remains
  Flutter + SDL3 runtime ownership with Cocos removed from the main frame loop.
- Keep `KRKR2_ANDROID_EGL_SAVE_GL_STATE=1` as the compatibility switch if
  Cocos GL state interaction becomes visible while Cocos is still in the host
  path.

## Verification performed locally

- `git diff --check` passed.
- `clang-format` was not installed in this environment, so no formatter command
  could be run locally.
- Java/Android local build still cannot be run here because `java` is not
  installed.

## Next migration steps

1. Push this recovery patch and use GitHub Actions for Android build
   verification.
2. Continue moving presenter ownership away from Cocos:
   - keep using `iTVPRenderManager::PrepareTextureForExternalPresenter()`
     instead of exposing raw `TVPSetRenderTarget(GLuint)` to presenters
   - keep the Android EGL presenter full-frame by default
   - keep dirty rect + pitch-aware upload at texture/cache boundaries
3. Consider porting the krkrsdl3-style renderer module more directly:
   - one module owns GL program/VAO/VBO/upload texture
   - per-frame loop clears/draws/swaps complete frames
   - upload path uses `GL_UNPACK_ROW_LENGTH` when available
   - no hot-path checks beyond required state changes
4. Keep memory budgets aligned with AetherKiri:
   - current defaults are 64 MiB single texture and 256 MiB total SDL_GPU cache
   - env overrides remain
     `KRKR2_SDL_GPU_MAX_SINGLE_TEXTURE_MB` and
     `KRKR2_SDL_GPU_TEXTURE_CACHE_MB`
