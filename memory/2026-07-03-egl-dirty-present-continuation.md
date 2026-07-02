# 2026-07-03 EGL dirty present continuation

## User hard requirements

- Target project is only `KiriKiri-LauncherC`.
- Other folders under `/root/kiriki-work` are reference-only: `AetherKiri`, `docs`, `kirikiroid2-web`, `KrKr2-Next`, `krkrsdl2-main`, `krkrsdl3-main`, `SDL-release-3.4.10`.
- Hard architecture goal: migrate toward Flutter + SDL3 and gradually remove Cocos.
- Rendering requirements: complete implementation, high performance, high compatibility, avoid excessive defensive checks/logging in the render hot path.
- Current requested direction: not a cosmetic change, but real `dirty rect + pitch-aware upload + GPU/CPU dual residency`.

## Latest logs inspected

Logs:

- `/root/log/20260703040151766.log`
- `/root/log/20260703040302278.log`
- `/root/log/20260703040333012.log`
- `/root/log/78.log`

Findings:

- `20260703040151766.log` is the native OpenGL/EGL path.
  - Runtime still reports `runtimeHost=cocos2d`.
  - SDL takeover starts around `2026-07-03 04:02:03.295`.
  - EGL native path is active:
    - `nativeGL=5`
    - `softwareUpload=0`
    - `present-texture-egl ... nativeGL=1 cpuCopyFree=1`
  - The logged dirty/source rectangle was still full-screen every sample:
    - `rect=0,0,1920x1080`
    - `dirty=0,0,1920x1080`
    - `fullFrame=1`
  - Sample timing:
    - `#1` at `04:02:03.344`
    - `#64` at `04:02:06.628`
    - `#128` at `04:02:09.710`
    - `#256` at `04:02:32.558` after a lifecycle gap
  - Short active window is around 18-20 FPS, not clearly better than software.

- `20260703040302278.log` is the software upload path.
  - Runtime still reports `runtimeHost=cocos2d`.
  - EGL software path is active:
    - `nativeGL=0`
    - `softwareUpload=1`
    - `cpuCopyFree=0`
  - It also presented full-screen dirty rectangles every sample.
  - Sample timing:
    - `#1` at `04:03:10.381`
    - `#64` at `04:03:14.470`
    - `#128` at `04:03:18.429`
    - `#256` at `04:03:22.597`
  - This explains the user report that OpenGL is still not better than software: nativeGL avoids CPU upload, but downstream still does full-surface present work and upstream dirty tracking still often collapses to full-screen.

- `20260703040333012.log` is tiny launcher-only noise.
- `78.log` contains a lot of system/other-app noise. Focus only on lines with tags such as `[core]`, `[render]`, `krkr2`, `android-egl-presenter`, `sdl-gpu-presenter`.

## Root cause refined

The previous patch added SDL_GPU dirty/pitch upload and OpenGL texture dirty tracking, but OpenGL render-manager still had this hot-path behavior:

- `tTVPOGLTexture2D_mutatble::AsTarget()` always called:
  - `MarkDirtyRect(tTVPRect(0, 0, Width, Height))`
- Almost every GPU operation binds the target through `AsTarget()`.
- Therefore even small `OperateRect`, `OperateTriangles`, copy, or fill operations caused the final window texture dirty rect to become full-screen.
- `TVPSDLTryPresentTexture()` then had no useful dirty data to consume, so Android EGL presenter always received `0,0,1920x1080`.

This was not just a logging problem. It prevented the new dirty upload/present path from being observable.

## Patch made in this continuation

Files edited:

- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

### RenderManager_ogl.cpp

Changed dirty tracking from target-bind based to actual-write based:

- Removed full-texture dirty marking from `tTVPOGLTexture2D_mutatble::AsTarget()`.
- Added exact dirty marking to:
  - `OperateRect()` custom fast path success: `tar->MarkDirtyRect(rctar)`
  - `OperateRect()` shader draw path: `tar->MarkDirtyRect(rctar)`
  - `OperateTriangles()` shader draw path: `tar->MarkDirtyRect(rcclip)`
  - `CopyTexture()` `glCopyImageSubData` fast path: mark copied destination size
  - `CopyTexture()` shader fallback: mark copied destination size
- Kept manual public `SetRenderTarget(iTVPTexture2D*)` conservative:
  - It still marks the whole target dirty after binding.
  - This protects plugins/manual render paths that bind a target and draw outside the render-manager wrapper without reporting a clip.

Important rationale:

- Internal render-manager operations already receive a target rect/clip, so they should report that precise dirty rect.
- Public manual `SetRenderTarget()` cannot know the later draw bounds, so full dirty is safer for compatibility.

### SDLAndroidFlutterPresenter.cpp

Added conservative Android EGL partial present support:

- `TVPAndroidEGLSurfacePresenterState` now tracks:
  - `preserveSwapBehavior`
  - `surfaceHasContent`
- On EGL window surface creation:
  - If the EGL config supports `EGL_SWAP_BEHAVIOR_PRESERVED_BIT`, try `eglSurfaceAttrib(..., EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED)`.
  - Query `EGL_SWAP_BEHAVIOR` to confirm.
  - Log `preserve=1/0` in `surface ready`.
- During present:
  - If `preserveSwapBehavior == true`, `surfaceHasContent == true`, and dirty rect is not full-screen, use dirty rect + `glScissor`.
  - Otherwise keep the old full-frame clear/draw behavior.
  - `present-android-egl` log now includes `fullFrame=0/1`.
  - `TVPSDLTexturePresentResult` now reports dirty source rect only when EGL actually used partial present. If EGL fell back to full-frame, it reports full-frame honestly.

Compatibility stance:

- No fake partial-present reporting.
- No partial draw on EGL surfaces without preserved swap behavior.
- First frame and every recreated surface still full-frame.

## Reference project notes relevant here

Subagent/reference findings:

- `krkrsdl3-main`:
  - `cpp/krkrsdl_gl.cpp` uses `glTexSubImage2D` with `GL_UNPACK_ROW_LENGTH = pitch / 4`, avoiding per-row repack when pitch is representable.
  - `cpp/environ/MainWindowLayer.cpp` updates existing window texture from raw pointer + pitch.
  - `cpp/krkrsdl.cpp` keeps the loop simple: run app, recycle textures, draw current window/overlay, swap.

- `krkrsdl2-main`:
  - Uses dirty rect to update only changed texture regions before render/present.
  - Device-lost/full invalidate logic is a useful fallback strategy.

- `AetherKiri`:
  - `GodotTexture2D` keeps CPU storage + GPU handle state.
  - `EnsureGpuHandle()` uploads CPU dirty data when needed.
  - `EnsureCpuReadable()` readbacks only when CPU access is needed.
  - GPU fast paths exist for copy/fill/blend before falling back to CPU.

## Current implementation state after patch

- SDL_GPU path already has:
  - persistent upload transfer buffer
  - pitch-aware or compact adaptive upload
  - full/partial upload counters
  - GPU/CPU resident flags in cache records
- OpenGL path now reports precise dirty rect for common render-manager write operations.
- Android EGL presenter now can consume dirty rect for real partial drawing only when preserved swap behavior is available.
- Cocos is still present and still reported as `runtimeHost=cocos2d`; this remains a major migration item.

## Validation done locally

- `git diff --check` passed.
- Local compile/build could not be run because this environment lacks normal local build tools (`cmake`, `ninja`, `clang++`, `g++`, `c++` were previously found absent).

## What to check in next logs

Look for:

- `surface ready ... preserve=1`
  - If `preserve=0`, EGL partial cannot activate on that device/config and full-frame present is expected.
- `present-android-egl ... rect=... fullFrame=0`
  - This means real dirty scissor present happened.
- `present-texture-egl ... dirty=... fullFrame=0`
  - This means render-manager dirty propagated through `TVPSDLTryPresentTexture()`.
- If logs still show `dirty=0,0,1920x1080 fullFrame=1`, then either:
  - the game really modifies the whole window texture per frame,
  - a public/manual render path uses `SetRenderTarget()` and forces full dirty,
  - or another code path still marks full dirty.

Potential next patch:

- Add a sampled, env-gated dirty source diagnostic that counts which full-dirty source is responsible:
  - `AsTarget` no longer does this internally, so likely suspects would be public `SetRenderTarget()`, `SetSize()`, full `Update()`, or whole-window operations.
- Continue Cocos removal by moving Android activity/runtime host away from `Cocos2dxActivity` and `TVPAppDelegate`.
- Move more AetherKiri-style GPU fast paths into the SDL3 render manager: copy, fill, alpha blend, then delayed CPU readback.
