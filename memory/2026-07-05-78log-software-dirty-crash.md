# 2026-07-05 78.log software dirty crash fix

## Scope and hard constraints

- Only modify `/root/kiriki-work/KiriKiri-LauncherC`.
- Other projects under `/root/kiriki-work` are reference-only:
  - `AetherKiri`
  - `docs`
  - `kirikiroid2-web`
  - `KrKr2-Next`
  - `krkrsdl2-main`
  - `krkrsdl3-main`
  - `SDL-release-3.4.10`
- Hard target remains migration to Flutter + SDL3 and gradual removal of
  Cocos2dx.
- User requirements remain:
  - complete implementation
  - high performance
  - high compatibility
  - avoid hot-path graphical integrity validation not present in reference
    projects
  - logs light by default, deeper logs behind switches such as
    `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`

## Log inspected

File:

- `/root/log/78.log`

Important runs found in this one log:

- `07-04 11:38:24`
  - process `18361`
  - native crash: `Aborted`
  - system copied `/data/tombstones/tombstone_04`
  - this host does not have `/data/tombstones`, so only the event was visible
    locally.
- `07-04 11:39:01`
  - process `19264`
  - native crash: `Segmentation fault`
  - tombstone text was present in `78.log` itself.
- `07-04 11:39:03` to `07-04 11:39:29`
  - process `20688`
  - no native crash in the visible window.
  - still showed input pressure:
    - `maxAgeMs=834`
    - `maxBacklog=25`
    - many coalesced/dropped touch events.

## Relevant native backtrace

Crash:

- pid: `19264`
- tid: `20864`
- thread name: `GLThread 76456`
- signal: `SIGSEGV`
- fault addr: `0x8`
- cause: null pointer dereference

Top frames:

- `TVPDeallocateRegionRect(tTVPRegionRect*)`
- `tTVPComplexRect::Clear()`
- `tTVPSoftwareTexture2D_static::MarkDirtyRect(tTVPRect const&)`
- `tTVPSoftwareTexture2D::GetScanLineForWrite(unsigned int)`
- `tTVPRenderMethod_Copy<...>::PartialCopy(...)`
- `TVPExecThreadTask(...)`
- `tTVPSoftwareRenderManager::OperateRect(...)`
- `iTVPBaseBitmap::CopyRect(...)`
- `tTJSNI_BaseLayer::Draw(...)`
- `tTJSNI_BaseLayer::InternalComplete2(...)`
- `tTJSNI_BaseLayer::CompleteForWindow(...)`
- `tTVPDrawDevice::Update()`
- `TVPDeliverWindowUpdateEvents()`
- `TVPMainScene::update(float)`
- `cocos2d::Scheduler::update(float)`
- `cocos2d::Director::drawScene()`
- `org.cocos2dx.lib.Cocos2dxRenderer.onDrawFrame`

Interpretation:

- The crash is not in Android EGL presentation itself.
- The crash is in the software render manager path while software completion is
  writing to a texture through `GetScanLineForWrite()`.
- `GetScanLineForWrite()` marks dirty per row.
- Many software render methods call `TVPExecThreadTask()` and write rows in
  parallel.
- Therefore `tTVPSoftwareTexture2D_static::MarkDirtyRect()` is a hot,
  potentially parallel path.

## Root cause

The previous commit `6ae21ca Gate clean GL presents and keep dirty regions`
changed `tTVPSoftwareTexture2D_static` dirty storage from:

- `bool AdapterDirty`
- `tTVPRect AdapterDirtyRect`

to:

- `tTVPComplexRect AdapterDirtyRegion`

That was correct in spirit for presenter-facing dirty region propagation, but
wrong for this software texture hot path.

Why:

- `tTVPComplexRect` is a linked-list region container.
- It allocates/frees `tTVPRegionRect` through global region-rect freelist
  functions:
  - `TVPAllocateRegionRect()`
  - `TVPDeallocateRegionRect()`
- Those structures are not suitable for parallel row-marking from software
  renderer worker tasks.
- Per-row writes would also add linked-list allocation/merge overhead in an
  extremely hot path.
- The reference direction is dirty region propagation at stable frame/update
  boundaries, not linked-list mutation on every scanline write.

## Patch applied

File changed:

- `cpp/core/visual/RenderManager.cpp`

Patch details:

- In `tTVPSoftwareTexture2D_static`, reverted dirty storage to:
  - `bool AdapterDirty`
  - `tTVPRect AdapterDirtyRect`
- `MarkDirtyRect()` now:
  - clips to texture bounds
  - returns for empty rect
  - unions into `AdapterDirtyRect` with `do_union()` when already dirty
  - stores the first dirty rect directly when clean
- `PeekDirtyRect()` and `ConsumeDirtyRect()` now use the single bounding rect.
- `PeekDirtyRegion()` and `ConsumeDirtyRegion()` remain available for the
  interface, but they wrap the single bounding rect into a temporary
  `tTVPComplexRect` for callers that ask for region API.
- `ClearAdapterDirtyRect()` clears the bool and rect.
- The Cocos fallback adapter upload path for software textures now consumes a
  single dirty rect and calls `UploadAdapterDirtyRect()`, instead of allocating
  a temporary `tTVPComplexRect` just to upload one bounding rect.
- The compressed software texture adapter path received the same single-rect
  upload adjustment.

## Why this is the right compromise

- It fixes the observed crash by removing `tTVPComplexRect` mutation from the
  parallel software write path.
- It restores the old low-overhead behavior for software-rendered textures.
- It still preserves the new interface shape:
  - existing callers can ask for `PeekDirtyRegion()` or `ConsumeDirtyRegion()`;
  - software textures simply report one bounding rect as a region of one.
- It does not affect OpenGL texture dirty region storage, which is still useful
  because OGL texture dirty marking is not the same per-scanline software worker
  path.
- It follows the user's performance concern:
  - do not add hot-path validation;
  - do not allocate/merge complex region objects for every scanline;
  - keep dirty rect + pitch-aware upload direction.

## Verification performed locally

- `git diff --check` passed.
- `clang-format` was not available locally:
  - no `clang-format`, `clang-format-18`, `clang-format-17`, etc. in PATH.
  - CI uses `DoozyX/clang-format-lint-action@v0.20` with clang-format 18.
- Android Gradle build was not runnable locally because `java` is not installed
  in this environment.
- Full compile verification must happen through GitHub Actions after push.

## Current expected behavior after this patch

Expected improvement:

- The `TVPDeallocateRegionRect -> tTVPComplexRect::Clear ->
  tTVPSoftwareTexture2D_static::MarkDirtyRect` crash should be gone.
- Software dirty marking should become cheaper than `6ae21ca`, especially in
  text/line-heavy software render paths.

What this patch does not solve by itself:

- Cocos still owns the outer runtime host and GL thread:
  - the stack still reaches `cocos2d::Director::drawScene()`.
- Input queue latency can still happen when startup scripts, layer completion,
  or Cocos-hosted update loops monopolize the GL thread.
- OpenGL/EGL visual issues and low FPS still require continuing the
  Flutter + SDL3 migration.

## Next migration direction

Keep the architectural direction from prior memory files:

- `BasicDrawDevice::Show -> RuntimePresenter -> SDLAndroidFlutterPresenter`
  should become the normal Android game-picture path.
- Cocos `WindowLayer::UpdateDrawBuffer()` should remain fallback only while the
  SDL/Flutter presenter stabilizes.
- Do not restore full dirty marking on `SetRenderTarget()`.
- Do not use `tTVPComplexRect` inside per-scanline software write hot paths.
- Carry richer dirty regions at frame/update boundaries where they are stable,
  not inside parallel row writes.
- Continue reducing Cocos ownership of:
  - main loop
  - input dispatch
  - window layer
  - video overlay

## Useful files for the next agent

- Crash fix:
  - `cpp/core/visual/RenderManager.cpp`
    - `tTVPSoftwareTexture2D_static`
    - `tTVPSoftwareTexture2D::GetScanLineForWrite`
- Dirty API:
  - `cpp/core/visual/RenderManager.h`
    - `iTVPTexture2D::MarkDirtyRegion`
    - `PeekDirtyRegion`
    - `ConsumeDirtyRegion`
- OGL dirty region still present:
  - `cpp/core/visual/ogl/RenderManager_ogl.cpp`
    - `tTVPOGLTexture2D::TextureDirtyRegion`
- Runtime presenter path:
  - `cpp/core/visual/impl/BasicDrawDevice.cpp`
  - `cpp/core/environ/runtime/RuntimePresenter.h`
  - `cpp/core/environ/sdl/SDLRuntimePresenter.cpp`
  - `cpp/core/environ/sdl/SDLGameManager.cpp`
  - `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- Current Cocos fallback/hybrid host:
  - `cpp/core/environ/cocos2d/MainScene.cpp`
  - `cpp/core/environ/cocos2d/CocosRuntimeHost.cpp`
  - `cpp/core/environ/cocos2d/AppDelegate.cpp`
