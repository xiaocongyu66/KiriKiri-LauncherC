# 2026-07-03 dirty rect / pitch-aware upload / dual residency memory

## Hard direction

- Project to modify: `/root/kiriki-work/KiriKiri-LauncherC` only.
- Reference projects remain read-only unless the user explicitly changes scope.
- User's hard requirement remains Flutter + SDL3 migration, gradually removing
  Cocos, with complete, high-performance, high-compatibility implementation.
- Avoid adding heavy render hot-path verification. Previous logs and user
  feedback indicate excessive diagnostics/validation can lower frame rate.
- Current optimization direction is:
  - dirty rect propagation,
  - pitch-aware upload,
  - GPU/CPU dual-resident texture state,
  - eventually GPU fast paths for Copy/Fill/AlphaBlend style operations,
  - keep software fallback for compatibility.

## Latest logs analyzed

Files:

- `/root/log/78.log`
- `/root/log/20260702115850636.log`

Important observations:

- Latest build was running the recent EGL presenter code:
  - `present-android-egl` appears repeatedly through `#512`.
  - `present-texture-egl` logs show `nativeGL=1 cpuCopyFree=1`.
  - No visible `present-flutter-direct` fallback in the relevant scan.
  - No visible `reason=no-native-gl` in the relevant scan.
- EGL native path is active, but frame rate was still low:
  - `present-android-egl #1` around `11:58:55.291`
  - `present-android-egl #512` around `11:59:23.697`
  - This is roughly 511 frames over 28.4 seconds, about 18 FPS.
- Presenter samples still showed full-frame:
  - `rect=0,0,1920x1080`
  - `dirty=0,0,1920x1080`
  - `fullFrame=1`
- Input queue showed pressure and latency:
  - queued roughly 1598,
  - drained roughly 597,
  - dropped roughly 109,
  - coalesced roughly 892,
  - `maxSeenAgeMs=533`.
- `clickglyph.asd` appeared many times in `LoadScenario`, but subagent analysis
  found `CacheLoad` for `clickglyph.asd` only once. Therefore it is not repeated
  cold loading from XP3/disk; it is repeated KAG scenario switching/state churn.
- Cocos remains in startup/runtime:
  - logs still show `runtimeHost=cocos2d` / Cocos runtime host registration.
  - A Cocos `CCTexture2D` GL error remains early in startup, pointing to
    leftover legacy host/texture paths.

## Subagent findings to preserve

### Log/source tracing

Relevant source points:

- `KAGParser.cpp`
  - `tTJSNI_KAGParser::LoadScenario()` emits `[kag] LoadScenario`.
  - `TVPGetScenario()` is the scenario cache entry.
  - `tTVPScenarioCacheItem::LoadScenario()` emits `CacheLoad`, meaning real cold
    cache load.
- `LayerIntf.cpp`
  - `tTJSNI_BaseLayer::AllocateImage()` emitted `Layer::AllocateImage`.
- `SDLGameManager.cpp`
  - `QueueAndroidInputEvent()` queues Android/Flutter touch.
  - `TVPSDLProcessAndroidInputQueue()` drains/coalesces.
  - `TVPSDLTryPresentTexture()` builds Android present plan.
- `SDLAndroidFlutterPresenter.cpp`
  - `TryPresentAndroidEGLSurfaceTexture()` emits `present-android-egl`.
  - `TryPresentAndroidTexturePlan()` previously forced EGL result to full-frame
    even when a dirty rect was passed.

Conclusion:

- Primary bottleneck is not repeated `clickglyph.asd` disk IO.
- Primary bottleneck is that EGL/presenter side was still effectively operating
  as full-frame present after takeover.
- Input pressure amplifies the problem because the main loop drains input once
  per frame and slow frames increase touch age/coalescing/drop counts.

### Reference projects

Most relevant reference designs:

- `krkrsdl3-main`
  - `cpp/krkrsdl_gl.cpp`: GL texture update uses row length / pitch-aware
    upload style.
  - `cpp/environ/MainWindowLayer.cpp`: final window texture update pulls
    `iTVPTexture2D::GetTextureData()` and pitch, reusing texture when size is
    unchanged.
  - `cpp/krkrsdl.cpp`: frame loop is SDL driven: run app, recycle textures,
    draw current window/overlay, swap.
- `krkrsdl2-main`
  - Uses dirty rect texture update before present.
  - Useful for dirty present and recreate/invalidate strategy.
- `AetherKiri`
  - `GodotTexture2D` keeps CPU storage + GPU handle state.
  - `EnsureGpuHandle()` uploads when CPU dirty and can discard CPU storage after
    GPU upload.
  - `EnsureCpuReadable()` reads back only when script/software path needs CPU
    pixels.
  - `OperateRect()` has GPU fast paths for common operations and software
    fallback.

Migration order implied by references:

1. Fix dirty rect propagation to final presenter.
2. Make upload pitch-aware and reuse upload buffers.
3. Track GPU/CPU residency state in texture cache.
4. Add GPU fast paths for common operations (`Copy`, `FillARGB`,
   `AlphaBlend`) without removing software fallback.
5. Continue extracting runtime/input/present loop from Cocos into SDL/Flutter
   host.

## Code changes made in this turn

### EGL dirty rect and full-frame behavior

Files:

- `cpp/core/environ/sdl/SDLGameManager.cpp`
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/visual/ogl/RenderManager_ogl.cpp`

Changes:

- `TVPSDLTryPresentTexture()` no longer treats every GL-backed texture as
  requiring a takeover full-frame present.
- Full-frame is now forced only when takeover has not presented yet, or when a
  surface-change force-full-frame flag is consumed.
- `tTVPOGLTexture2D` now owns dirty state:
  - `TextureDirty`
  - `TextureDirtyRect`
  - overrides `MarkDirtyRect`, `PeekDirtyRect`, and `ConsumeDirtyRect`
- OGL CPU uploads/initialization now mark dirty rects from the texture side.
- OGL FBO target entry (`AsTarget`) conservatively marks the whole logical
  texture dirty. This avoids frame skips after removing presenter-side
  `glBackedTexture` force-full-frame. It is intentionally conservative until
  the render methods pass exact target rects into the texture dirty state.
- Important correction: the current EGL presenter still draws a full-screen
  quad and swaps the Android EGL surface. Dirty rects are passed into the EGL
  path so software texture upload can avoid full CPU->GPU upload, but native GL
  EGL presentation is still a full-surface draw unless a later patch adds
  `eglSwapBuffersWithDamageKHR/EXT` plus safe buffer-age handling.
- `TryPresentAndroidTexturePlan()` therefore still reports EGL present as
  `FullPresentRect(...)` / `fullFrame=true`, because that is the honest current
  presentation behavior.

Rationale:

- Previous code was safe but too conservative for deciding when to force a TVP
  texture full-frame dirty update. EGL surface presentation itself still needs
  a separate damage-extension implementation before it can safely become true
  partial-surface present.
- The correct ownership is now: render textures know whether they are dirty;
  presenter consumes that state. Presenter should not infer dirty state from
  `GetNativeGLTextureId()` alone.

### Input/log hot-path reduction

Files:

- `cpp/core/environ/sdl/SDLGameManager.cpp`
- `cpp/core/base/KAGParser.cpp`
- `cpp/core/visual/LayerIntf.cpp`

Changes:

- `IsHighFrequencyInput()` now treats touch begin/end/move/cancel as
  high-frequency. This reduces logcat/string-format overhead during stress
  tapping.
- `TVPSDLProcessAndroidInputQueue()` no longer logs every batch merely because
  it coalesced move events. It still logs sampled batches, drops, and high
  latency (`maxAgeMs > 250`).
- Android KAG diagnostics are now disabled by default and require:
  - `KRKR2_ENABLE_KAG_DIAGNOSTICS=1`
- Android layer allocation diagnostics are now disabled by default and require:
  - `KRKR2_ENABLE_LAYER_ALLOC_DIAGNOSTICS=1`

Rationale:

- The latest `78.log` was very large and contained high-frequency input,
  `LoadScenario`, and `Layer::AllocateImage` diagnostic output.
- The user explicitly warned against too much hot-path verification/logging.

### SDL_GPU pitch-aware upload and transfer buffer reuse

Files:

- `cpp/core/render/sdlgpu/SDLGpuBackend.cpp`

Changes:

- `Backend::Impl` now owns a persistent upload transfer buffer:
  - `SDL_GPUTransferBuffer *uploadTransferBuffer`
  - `uint32_t uploadTransferBufferSize`
- `EnsureUploadTransferBuffer(size)` creates or grows the transfer buffer.
- `UploadTexture2D()` no longer creates/releases a transfer buffer per upload.
- Upload mapping now uses `SDL_MapGPUTransferBuffer(..., cycle=true)`, matching
  SDL3's intended ring-buffer/cycling model for in-flight uploads.
- `SDL_UploadToGPUTexture(..., cycle=...)` now cycles the destination texture
  only for full-texture uploads. Partial uploads keep cycling disabled because
  a cycled internal texture may not preserve old pixels outside the dirty rect.
- Upload layout is pitch-aware:
  - when source pitch is expressible as pixels and not much larger than the
    dirty row size, `pixels_per_row = pitch / bytesPerPixel`;
  - when dirty rect is narrow and pitch would waste too much transfer memory,
    it compacts rows and uses `pixels_per_row = width`.
- Transfer buffer size is based on upload row pitch times height.
- Tight uploads use a single `memcpy`; non-tight uploads copy dirty row bytes
  into the mapped transfer buffer with the selected upload pitch.

Rationale:

- This follows SDL3 docs: transfer buffers can be reused with `cycle=true`.
- It avoids per-frame/per-dirty-rect transfer-buffer create/map/free churn.
- It combines pitch-aware upload for near-full-width rows with compact upload
  for small dirty rects.

### SDL_GPU texture cache dual-resident state

Files:

- `cpp/core/render/sdlgpu/SDLGpuTextureCache.h`
- `cpp/core/render/sdlgpu/SDLGpuTextureCache.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

Changes:

- `TextureCache::Record` now tracks:
  - `cpuResident`
  - `gpuResident`
  - `hasCpuDirtyRect`
  - `cpuDirtyRect`
  - `fullUploads`
  - `partialUploads`
- `TextureCacheStats` now tracks:
  - `gpuResidentBytes`
  - `fullUploads`
  - `partialUploads`
- `TextureCache::Upsert()`:
  - creates GPU texture when needed,
  - records/merges CPU dirty rect,
  - full uploads when record is new, GPU copy is not resident, or caller asks
    for full upload,
  - partial uploads when GPU copy is resident and a dirty rect is available,
  - marks the GPU copy resident after upload,
  - clears CPU dirty state after upload.
- `DestroyRecord()` subtracts `gpuResidentBytes` if a GPU-resident copy is
  destroyed.
- SDL overlay/log info now includes `full=` and `partial=` upload counters.
- Android present log includes `gpuFull=` and `gpuPartial=` counters.

Rationale:

- This is the first concrete GPU/CPU dual-residency step.
- It does not yet implement GPU-side blend/copy/fill fast paths, but provides
  the state and accounting needed for that next step.

## Verification status

- `git diff --check` passed with no output.
- Local machine currently lacks:
  - `cmake`
  - `ninja`
  - `clang++`
  - `g++`
  - `c++`
- Therefore local native build verification is not possible in this container.
- Android/Flutter build verification should be done via GitHub Actions after
  pushing.

## Follow-up priorities

1. Push/CI once the user requests or after the next commit is prepared.
2. Inspect CI build for compile issues around:
   - SDL_GPU transfer buffer lifetime,
   - new `TextureCacheStats` fields,
   - `tTVPRect` use in `SDLGpuTextureCache.h`.
3. Next performance step:
   - implement Android EGL damage path:
     `eglSwapBuffersWithDamageKHR/EXT`, extension detection, buffer age, and
     scissor/sub-quad rendering only when preserved/aged buffers make it safe.
   - implement SDL_GPU fast paths for the most common render operations:
     `Copy`, `FillARGB`, `AlphaBlend` or project-local equivalents.
   - Keep software fallback and mark CPU/GPU dirty state correctly.
4. Cocos removal next targets from explorer:
   - Android `KR2Activity extends Cocos2dxActivity`,
   - `TVPAppDelegate : cocos2d::Application`,
   - `TVPCocosRuntimeHost`,
   - `TVPMainScene` window/present/input dependencies,
   - `krkr::Texture2D = cocos2d::Texture2D` alias in visual layer.
