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

The intended model after this change:

- scanline write access is just pixel access
- dirty ownership sits at stable operation/frame boundaries
- `Update()` and `SetPoint()` still explicitly mark their exact dirty area
- presenter/upload code consumes the resulting dirty rect at present time

## Patch applied

File changed:

- `cpp/core/visual/RenderManager.cpp`

Details:

1. Removed per-row dirty marking from:

   - `tTVPSoftwareTexture2D::GetScanLineForWrite(tjs_uint l)`

   It now only clears the opacity flag and returns the writable scanline.
   This avoids row-by-row dirty union work during hot render loops.

2. Added explicit operation-boundary dirty marks to software triangle and
   perspective paths that previously depended on scanline side effects:

   - `tTVPSoftwareRenderManager::OperateTriangles(...)`
     - OpenCV warp path now marks `rcclip` after `DoRender()`.
     - `InternalAffineBlt` triangle path now marks `rcclip` once after all
       threaded triangle work completes.
   - `tTVPSoftwareRenderManager::OperatePerspective(...)`
     - non-rect OpenCV perspective path now marks `rcclip` after `DoRender()`.

3. Existing rectangle path remains unchanged:

   - `tTVPSoftwareRenderManager::OperateRect(...)` still marks `rctar` once
     after the render method completes.

4. Existing point/update paths remain unchanged:

   - `tTVPSoftwareTexture2D::Update(...)` marks the update rect.
   - `tTVPSoftwareTexture2D::SetPoint(...)` marks the single pixel.

## Why this is low risk

- Most software renderer writes already go through `OperateRect()`, which
  marks the true target rect after rendering.
- The only software render-manager paths that could rely on scanline dirty
  side effects were the non-rect triangle/perspective paths; this patch adds
  explicit marks for them.
- External `GetScanLineForWrite()` calls found by `rg` are mostly bitmap
  interfaces (`iTVPBaseBitmap`, transition providers, plugin bitmaps), not
  `iTVPTexture2D`.
- `cpp/core/visual/gl/ResampleImage.cpp` uses `iTVPBaseBitmap`, not the
  software texture class changed here.
- `Update()` and `SetPoint()` still mark dirty directly, so direct small writes
  remain covered.
- No mutex or complex validation was added to the hot path.

## Expected effect

Expected performance improvement:

- Less dirty bookkeeping during text-heavy and row-heavy software rendering.
- Less parallel contention/race exposure on `AdapterDirtyRect` union state.
- Dirty state is now updated once per render operation rather than once per
  writable scanline.

Expected correctness:

- Software `OperateRect()` dirty propagation remains the same from the
  presenter's point of view.
- Non-rect triangle/perspective paths still dirty the affected clip region.
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
- Local Android Gradle build still cannot be run in this environment because
  Java is not installed.
- Local `clang-format` is not available; CI format check remains the source of
  formatting verification.

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
   boundaries.
