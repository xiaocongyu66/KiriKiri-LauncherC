# 2026-07-05 Hard Target: Remove Cocos, Move Rendering To Flutter + SDL3

## Non-negotiable direction

The project being changed is only:

- `/root/kiriki-work/KiriKiri-LauncherC`

Reference-only projects:

- `/root/kiriki-work/AetherKiri`
- `/root/kiriki-work/krkrsdl2-main`
- `/root/kiriki-work/krkrsdl3-main`
- `/root/kiriki-work/docs`
- `/root/kiriki-work/SDL-release-3.4.10`

Do not edit the reference projects.

The hard architectural target is:

- migrate the runtime to Flutter + SDL3;
- remove Cocos2dx from the game rendering path;
- do not keep polishing Cocos as the final renderer;
- treat any remaining Cocos code only as temporary bootstrap/compatibility code
  until the SDL3/Flutter path owns frame production, presentation, input, and
  lifecycle.
- keep the actual game/native external presenter buffer fixed at `1920x1080`;
  Flutter may contain/center that texture visually, but must not resize the game
  buffer to the full overlay or physical screen.

In Chinese, because the user explicitly asked to make this impossible to miss:

`硬性目标：迁移到 Flutter + SDL3 新架构，逐步并最终去除 Cocos2dx。不要继续把 Cocos 当成最终渲染链路修补。`

`硬性分辨率要求：游戏画面/native external presenter buffer 必须固定 1920x1080。Flutter 只负责把这个固定画面居中/等比显示和映射输入，不能把游戏 buffer 改成铺满屏幕。`

## Rendering pipeline rule

Do not keep taking one small module from one project and another unrelated
module from a different project unless the whole chain is integrated.

The render path must be a closed loop:

1. The engine produces a new composited frame.
2. The final texture/render target is prepared for external presentation.
3. Dirty rects are used only for upload/cache work.
4. The visible presenter draws/presents a deterministic full frame.
5. The presenter marks that a real external frame was posted.
6. Legacy Cocos-era `TVPForceSwapBuffer()` must not swap a stale/duplicate
   surface after SDL/Flutter has taken ownership.
7. If no new frame was produced, do not swap just because a tick happened.

This is the AetherKiri rule the user wants copied as a whole:

- `UpdateDrawBuffer()` creates the visible frame and marks it dirty.
- `TVPForceSwapBuffer()` swaps only when that produced-frame dirty flag exists.
- Unconditional tick-based swapping can alternate current/stale buffers and
  cause old/new split screens, black blocks, flicker, or stale layers.

This is the krkrsdl3 rule the user wants preserved:

- upload may be dirty/pitch-aware;
- final present is full-frame and deterministic;
- clear the full output first, then draw the complete game texture into the
  correct viewport.

## What was reinforced in the current implementation pass

Files touched for this rule:

- `cpp/core/visual/impl/DrawDevice.h`
- `cpp/core/visual/impl/DrawDevice.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/android/AndroidUtils.cpp`

Important behavior:

- `iTVPDrawDevice::SetViewport()` now exists.
- `tTVPDrawDevice` stores `ViewportRect` and uses it for input coordinate
  transforms.
- SDL/Flutter present computes an aspect-preserving viewport, sets
  `DestRect`, `ClipRect`, `Viewport`, and full output size together.
- SDL renderer fallback disables SDL logical stretch and explicitly clears the
  output then draws the full texture into the computed viewport.
- Android direct partial present is disabled; final direct present locks/posts
  full frame by default.
- Android EGL partial final present is disabled; EGL path clears and draws the
  full output frame.
- `TVPSDLAndroidIsExternalPresenterActive()` tells Android legacy swap code that
  SDL/Flutter owns presentation.
- `TVPForceSwapBuffer()` now skips legacy Cocos swaps once the external
  presenter is active, preventing a second stale Cocos swap after a correct
  SDL/Flutter frame.

## Performance rule

Avoid hot-path graphical validation:

- no checksums;
- no `glReadPixels` for validation;
- no per-frame `glGet*` state validation except when explicitly behind
  diagnostics;
- no noisy logs unless controlled by diagnostics switches.

The desired performance model is:

- GPU/GL native texture present when available;
- dirty rect + pitch-aware upload only for upload/cache;
- final visible present is full-frame GPU work;
- CPU fallback exists for compatibility, but should not become the main path
  when native GL is available.

## 2026-07-05 continuation: Aether-style deferred swap

The Android EGL/Flutter presenter was moved closer to the AetherKiri whole
chain instead of swapping immediately inside `UpdateDrawBuffer()`.

New chain in `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`:

1. `TryPresentAndroidEGLSurfaceTexture()` prepares the source first:
   `TVPGetRenderManager()->PrepareTextureForExternalPresenter(texture)`.
2. It then binds the Flutter `ANativeWindow` EGL surface, clears the full
   output, disables scissor/blend/depth, binds the source texture, and draws the
   full-screen quad.
3. After the quad draw it performs cheap Aether-like GL cleanup:
   disable vertex attrib arrays, unbind array buffer, unbind texture, and
   `glUseProgram(0)`.
4. It calls `glFlush()` and marks `state.frameDirty = true`; it does **not**
   call `eglSwapBuffers()` there anymore.
5. `TVPForceSwapBuffer()` now calls
   `TVPSDLAndroidSwapExternalPresenterIfDirty()` first.
6. `TVPSDLAndroidSwapExternalPresenterIfDirty()` consumes the dirty frame,
   temporarily makes the Flutter EGL surface current, swaps it once, restores
   the previous Cocos/engine EGL surface, records the successful EGL present,
   and only then updates the presented surface size.

This matches the AetherKiri rule more closely:

- render/update prepares the external target and marks "real new frame exists";
- the engine loop swap point performs the actual swap;
- ticks without new frames do not swap the external SurfaceTexture;
- legacy Cocos `eglSwapBuffers()` is skipped once SDL/Flutter has a dirty or
  already-presented external frame.

Why this matters:

- Swapping immediately from the presenter can happen before the Cocos-era
  `drawScene()` reaches its official swap point.
- Swapping again later, or leaving GL state from the presenter active, can
  produce alternating old/new buffers or stale regions.
- The new path keeps the Aether ordering: source prepare -> external draw ->
  mark dirty -> one swap at `TVPForceSwapBuffer()`.

Important constraints for future agents:

- Do not reintroduce Android EGL preserved-swap/partial-present logic.
- Do not make dirty rect control the visible present rectangle.
- Do not add hot-path `glGet*`, `glReadPixels`, checksums, or per-frame
  validation unless behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
- Do not restore bare `extern void TVPSetRenderTarget(GLuint)` in the
  presenter. Keep the detach inside
  `iTVPRenderManager::PrepareTextureForExternalPresenter()`.
- Cocos is still only a temporary loop/bootstrap. The hard target remains
  Flutter + SDL3 and removal of Cocos2dx from the game render path.
