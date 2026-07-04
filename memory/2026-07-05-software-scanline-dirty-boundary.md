# 2026-07-05 software scanline dirty boundary optimization

## Scope and hard constraints

- Project to modify: `/root/kiriki-work/KiriKiri-LauncherC` only.
- Reference-only projects:
  - `/root/kiriki-work/AetherKiri`
  - `/root/kiriki-work/docs`
  - `/root/kiriki-work/kirikiroid2-web`
  - `/root/kiriki-work/KrKr2-Next`
  - `/root/kiriki-work/krkrsdl2-main`
  - `/root/kiriki-work/krkrsdl3-main`
  - `/root/kiriki-work/SDL-release-3.4.10`
- Hard target is still migration to Flutter + SDL3, gradually removing the
  Cocos2dx host from the runtime presentation path.
- User's bottom-line requirements remain:
  - complete implementation
  - high performance
  - high compatibility
  - avoid hot-path graphical integrity validation not present in reference
    implementations
  - prefer dirty rect / pitch-aware upload / CPU-GPU dual residency /
    present gating
  - default logs must stay light; diagnostics should be behind switches such
    as `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`

## Reason for this change

The previous crash fix restored software texture dirty storage to:

- `bool AdapterDirty`
- `tTVPRect AdapterDirtyRect`

That removed `tTVPComplexRect` linked-list mutation from the parallel software
renderer write path and fixed the observed
`TVPDeallocateRegionRect -> tTVPComplexRect::Clear ->
tTVPSoftwareTexture2D_static::MarkDirtyRect -> GetScanLineForWrite` crash.

However, software rendering was still doing one dirty mark for every writable
scanline:

- `tTVPSoftwareTexture2D::GetScanLineForWrite(l)` called
  `MarkDirtyRect(tTVPRect(0, l, Width, l + 1))`
- many render methods call `GetScanLineForWrite()` inside per-row loops
- several render methods split the work through `TVPExecThreadTask()`

This is expensive and redundant:

- For most software render operations, `tTVPSoftwareRenderManager::OperateRect`
  already marks the final target rectangle once after `DoRender()` completes.
- The row dirty marks were also not precise for several methods that take one
  writable scanline pointer and then advance by pitch internally; in those
  cases only the first row was marked by the scanline call, and correctness
  depended on the outer operation-level mark anyway.
- The user specifically asked to remove excessive render validation/dirty
  bookkeeping from hot paths and follow reference-style update boundaries.

The intended model after the follow-up correction:

- public raw-buffer/plugin scanline writes keep the old dirty semantics
- internal software render-loop scanline writes use a no-dirty helper
- dirty ownership for render methods sits at stable operation/frame boundaries
- `Update()` and `SetPoint()` still explicitly mark their exact dirty area
- presenter/upload code consumes the resulting dirty rect at present time

## Patch applied

Files changed:

- `cpp/core/visual/RenderManager.h`
- `cpp/core/visual/RenderManager.cpp`
- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Details:

1. Added a new texture interface helper:

   - `iTVPTexture2D::GetScanLineForWriteNoDirty(tjs_uint l)`

   Default implementation falls back to `GetScanLineForWrite(l)`, so existing
   texture implementations keep their current behavior unless they explicitly
   opt in.

2. `tTVPSoftwareTexture2D` now has two write paths:

   - public `GetScanLineForWrite(l)` still marks
     `tTVPRect(0, l, Width, l + 1)`;
   - internal `GetScanLineForWriteNoDirty(l)` only clears opacity and returns
     the writable scanline.

   This preserves raw-buffer/plugin compatibility while removing dirty union
   work from software render methods.

3. Software render methods in `RenderManager.cpp` now use
   `GetScanLineForWriteNoDirty()` for their internal row writes. These writes
   are covered by the outer operation dirty marks.

4. Added explicit operation-boundary dirty marks to software triangle and
   perspective paths that previously depended on scanline side effects:

   - `tTVPSoftwareRenderManager::OperateTriangles(...)`
     - OpenCV warp path now marks `rcclip` after `DoRender()`.
     - `InternalAffineBlt` triangle path now marks `rcclip` once after all
       threaded triangle work completes.
   - `tTVPSoftwareRenderManager::OperatePerspective(...)`
     - non-rect OpenCV perspective path now marks `rcclip` after `DoRender()`.

5. Existing rectangle path remains unchanged:

   - `tTVPSoftwareRenderManager::OperateRect(...)` still marks `rctar` once
     after the render method completes.

6. Existing point/update paths remain unchanged:

   - `tTVPSoftwareTexture2D::Update(...)` marks the update rect.
   - `tTVPSoftwareTexture2D::SetPoint(...)` marks the single pixel.

7. Follow-up OpenGL compatibility fix:

   - `tTVPOGLTexture2D_mutatble::GetScanLineForWrite(l)` now also marks
     `tTVPRect(0, l, Width, l + 1)`.
   - This keeps public/raw scanline write semantics consistent between the
     software and OpenGL texture implementations.
   - OpenGL render-manager internal GPU operations are unaffected because they
     already mark dirty at operation boundaries (`OperateRect`,
     `OperateTriangles`, etc.) and do not use this public scanline path for
     hot GPU draws.

8. Android Flutter EGL presenter hot-path cleanup:

   - Added optional `KRKR2_ANDROID_EGL_SAVE_GL_STATE`.
   - By default, the presenter no longer snapshots/restores GL state when it is
     presenting through the same EGL context. That snapshot was a pile of
     `glGet*` calls on the present path. It can be re-enabled for compatibility
     testing with `KRKR2_ANDROID_EGL_SAVE_GL_STATE=1`.
   - `eglSwapInterval(display, 0)` is now issued once per EGL window surface
     instead of on every frame. The flag is reset when the presenter surface or
     context resources are dropped.
   - Software upload texture parameters are now set when the private upload
     texture is created. The per-frame `glTexParameteri` block after binding
     `state.uploadTexture` was removed.
   - The software upload `glGetError()` check now only runs when
     `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS` is truthy. This keeps default runtime
     closer to the reference projects: upload, draw, swap, and only diagnose
     when explicitly requested.

   Compatibility note:

   - The EGL presenter still restores the previous EGL draw/read surface and
     context every frame.
   - Only the optional GL object/state snapshot is skipped by default.
   - If a device or the remaining Cocos GL path depends on exact prior program,
     buffer, viewport, blend/scissor, or unpack state, use
     `KRKR2_ANDROID_EGL_SAVE_GL_STATE=1` as the fallback while the remaining
     Cocos host path is being removed.

## Why this is low risk

- Most software renderer writes already go through `OperateRect()`, which
  marks the true target rect after rendering.
- The only software render-manager paths that could rely on scanline dirty
  side effects were the non-rect triangle/perspective paths; this patch adds
  explicit marks for them.
- Public `tTVPSoftwareTexture2D::GetScanLineForWrite()` was intentionally kept
  dirty-marking because raw-buffer/plugin paths can still reach texture
  scanlines indirectly.
- Public `tTVPOGLTexture2D_mutatble::GetScanLineForWrite()` now also marks the
  written row dirty. This avoids stale/no-op presents for compatibility paths
  that write OpenGL-backed mutable texture CPU storage directly.
- A subagent specifically warned that raw-buffer paths such as
  `Layer.mainImageBufferForWrite`, `Bitmap.pixelBufferForWrite`, transition
  scanline providers, resample helpers, and motionplayer bitmap writes should
  not lose their existing public scanline dirty semantics.
- `cpp/core/visual/gl/ResampleImage.cpp` uses `iTVPBaseBitmap`, not the
  software texture class changed here.
- `Update()` and `SetPoint()` still mark dirty directly, so direct small writes
  remain covered.
- No mutex or complex validation was added to the hot path.
- The Android EGL presenter now avoids default hot-path `glGet*`, upload
  `glGetError`, repeated `eglSwapInterval`, and repeated upload texture
  parameter calls.

## Expected effect

Expected performance improvement:

- Less dirty bookkeeping during text-heavy and row-heavy software rendering.
- Less parallel contention/race exposure on `AdapterDirtyRect` union state.
- Dirty state is now updated once per render operation rather than once per
  writable scanline.
- Less GLES/EGL driver overhead on the Flutter EGL present path, especially on
  devices where state queries and redundant texture parameter calls serialize
  the driver.

Expected correctness:

- Software `OperateRect()` dirty propagation remains the same from the
  presenter's point of view.
- Non-rect triangle/perspective paths still dirty the affected clip region.
- External/raw-buffer write paths still call public `GetScanLineForWrite()`
  and therefore still mark dirty per row as before.
- Presenter paths using `PeekDirtyRect()` / `ConsumeDirtyRect()` continue to
  receive a bounded dirty area.

What this does not solve by itself:

- Cocos still owns part of the Android runtime host and old GL thread path.
- OpenGL-specific visual issues need continued comparison with AetherKiri and
  krkrsdl2/krkrsdl3.
- SDL_GPU support exists in `cpp/core/render/sdlgpu`, but it is still mostly a
  shadow-upload/cache path and not yet the full presenter replacing Cocos.

## Verification performed locally

- `git diff --check` passed.
- `clang-format-18` was installed locally and run on:
  - `cpp/core/visual/RenderManager.cpp`
  - `cpp/core/visual/RenderManager.h`
  - `cpp/core/visual/ogl/RenderManager_ogl.cpp` was manually kept to a
    one-line functional diff after checking the format pass would otherwise
    touch unrelated existing formatting.
- `SDLAndroidFlutterPresenter.cpp` was intentionally kept as a minimal manual
  diff. A broad `clang-format-18` pass was tested and then discarded because it
  reformatted almost the entire file.
- Local Android Gradle build still cannot be run in this environment because
  Java is not installed.

## CI state before this patch

Previous pushed commit:

- `984943b Fix software dirty region crash`

GitHub Actions result checked on 2026-07-05:

- `Code Format Check`: success
- `Build Flutter Android`: success
- Run URL:
  `https://github.com/xiaocongyu66/KiriKiri-LauncherC/actions/runs/28716139107`

## Important next steps

1. Push this patch and watch both GitHub Actions workflows.
2. If runtime FPS is still low, continue replacing Cocos-hosted presentation
   with Flutter + SDL3 paths instead of adding more validation to the old
   render loop.
3. The next safe migration area is still presenter modularization:
   - keep `BasicDrawDevice::Show -> RuntimePresenter -> SDLAndroidFlutterPresenter`
     as the normal game-picture path;
   - keep Cocos `WindowLayer::UpdateDrawBuffer()` as fallback only;
   - move more of the outer host/input/main-loop ownership into SDL3/Flutter.
4. For SDL_GPU work, reuse the existing modules:
   - `cpp/core/render/sdlgpu/SDLGpuBackend.*`
   - `cpp/core/render/sdlgpu/SDLGpuTvpAdapter.*`
   - `cpp/core/render/sdlgpu/SDLGpuTextureCache.*`
   - `cpp/core/environ/sdl/SDLGameManager.cpp`
5. Do not reintroduce per-scanline `tTVPComplexRect` or mutex-heavy dirty
   tracking. Dirty regions should be collected at operation or frame
   boundaries, while public raw scanline APIs preserve compatibility.
6. If Android EGL rendering shows Cocos-state regressions while Cocos is still
   in the host path, first test `KRKR2_ANDROID_EGL_SAVE_GL_STATE=1`; do not add
   unconditional `glGet*` state validation back to the default frame path.
