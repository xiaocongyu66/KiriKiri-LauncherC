# 2026-07-04 Runtime Present Dirty Plan

## User constraints

- Project being modified: `KiriKiri-LauncherC` only.
- Reference projects are read-only:
  - `AetherKiri`
  - `docs`
  - `kirikiroid2-web`
  - `KrKr2-Next`
  - `krkrsdl2-main`
  - `krkrsdl3-main`
  - `SDL-release-3.4.10`
- Hard target remains Flutter + SDL3.
- Continue removing Cocos from hot rendering paths.
- Prefer complete, high-performance, high-compatibility code.
- Avoid adding defensive validation or per-frame graphical integrity checks that
  the reference SDL2/SDL3 projects do not need.
- Current rendering direction:
  - dirty rect first
  - pitch-aware upload
  - CPU/GPU dual residency
  - light diagnostics by default, deeper logs only behind
    `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`

## Why this change exists

Before this continuation, `WindowLayer::UpdateDrawBuffer()` called
`TVPRuntimePresentHostWindowTexture()` with only texture, stage, and layer size.
The SDL side then had to call `texture->PeekDirtyRect()` again inside
`TVPSDLTryPresentTexture()`.

That still worked, but the request itself did not describe the frame update
plan. For the new Flutter + SDL3 architecture, the presenter boundary should
carry enough information to let the SDL presenter decide between:

- no-op / already-presented
- partial dirty present
- first/full-frame present
- native GL present-only path
- software/surface fallback path

This keeps Cocos as a temporary caller while moving frame ownership into the
runtime presenter interface.

## Implemented shape

- `TVPRuntimeTexturePresentRequest` now contains:
  - `texture`
  - `stage`
  - `layerWidth`
  - `layerHeight`
  - `hasDirtyRect`
  - `dirtyRect`
  - `forceFullFrame`
- `TVPWindowLayer::UpdateDrawBuffer()` now:
  - creates a named `TVPRuntimeTexturePresentRequest`
  - peeks `tex->PeekDirtyRect(dirty)` without consuming it
  - clips dirty to the texture bounds
  - passes the dirty rectangle through the runtime presenter request
  - falls back to the existing Cocos sprite upload only when runtime presenter
    cannot present
- `tTVPBasicDrawDevice::Show()` now also tries the runtime presenter directly
  after obtaining the draw-buffer texture:
  - success path returns before entering Cocos `WindowLayer::UpdateDrawBuffer()`
  - fallback still calls the Cocos window layer when presenter cannot present
  - this is an incremental step toward
    `BasicDrawDevice::Show -> RuntimePresenter -> SDLAndroidFlutterPresenter`
    as the main Android game-picture path
- `SDLRuntimePresenter` now forwards the complete request to SDL instead of
  unpacking only the old four fields.
- `SDLGameManager` now has request-based overloads:
  - `TVPSDLTryPresentTexture(const TVPRuntimeTexturePresentRequest &request)`
  - `TVPSDLPresentHostWindowTexture(tTJSNI_BaseWindow *window,
    const TVPRuntimeTexturePresentRequest &request)`
- Old SDL function signatures remain as compatibility wrappers.

## Performance intent

- Dirty ownership is still held by the texture; the caller only peeks and passes
  the plan.
- SDL still consumes the texture dirty rect after a successful present.
- The hot path does not add logging or pixel validation.
- The runtime presenter can now bypass Cocos `WindowLayer::UpdateDrawBuffer()`
  on successful presents while keeping Cocos as a fallback.
- This is a boundary refactor toward a Flutter + SDL3 runtime host, not a new
  renderer branch.

## Important follow-up

- The request still carries only one bounding dirty rect.
- Next improvement should mirror AetherKiri more closely:
  - keep multiple dirty regions with `tTVPComplexRect`
  - merge to a bounding rect only after a practical threshold
  - let Android/EGL presenter use subrects when direct partial present is
    enabled
- Do not restore the old full-frame dirty marking in `SetRenderTarget()`.
  Binding a target is not a write.

## Cocos surfaces still on the critical path

Read-only scan performed during this continuation:

- `cpp/core/environ/cocos2d/AppDelegate.cpp`
  - Still creates and owns `cocos2d::Director` and `GLViewImpl`.
  - Still creates `TVPMainScene` and runs it through Cocos.
  - Foreground/background lifecycle still calls Cocos animation controls.
- `cpp/core/environ/cocos2d/CocosRuntimeHost.cpp`
  - `TVPCocosRuntimeHost::StartGame()` enters `TVPMainScene::startupFrom()`.
  - `TVPCocosRuntimeHost::RunFrame()` calls `Application->Run()`.
  - Register function still combines Cocos host with SDL presenter.
- `cpp/core/environ/cocos2d/MainScene.cpp`
  - `TVPMainScene` is still the central scene, input, IME, UI stack, and
    window manager.
  - `TVPWindowLayer` still derives from Cocos `ScrollView`.
  - `TVPWindowLayer::UpdateDrawBuffer()` is now the main bridge point:
    - first tries runtime presenter
    - falls back to Cocos sprite upload
  - Android takeover hides Cocos nodes after SDL/Flutter presenter has taken
    the game surface, but Cocos still owns the outer loop.
- `cpp/core/visual/impl/BasicDrawDevice.cpp`
  - `tTVPBasicDrawDevice::Show()` is the engine present entry.
  - It still calls `form->UpdateDrawBuffer(tex)`, and the current Android form
    is the Cocos `TVPWindowLayer`.
- `cpp/core/visual/impl/WindowImpl.cpp`
  - `TVPCreateAndAddWindow()` still resolves to the Cocos window-layer
    implementation.
- `cpp/core/movie/ffmpeg/KRMoviePlayer.cpp`
  - Video overlay still uses Cocos `Node`/`TVPYUVSprite`.
  - This is a high-risk later migration area because it affects layer order and
    video sync.
- Android Java/Kotlin shell:
  - `KR2Activity` still extends `Cocos2dxActivity`.
  - `MainActivity` still derives from `KR2Activity`.
  - SDL Java and Flutter surface are already present, but Activity ownership is
    still mixed Cocos + SDL + Flutter.

Practical migration order:

1. Make `BasicDrawDevice::Show -> RuntimePresenter -> SDLAndroidFlutterPresenter`
   the stable Android game-picture path.
2. Move input/menu overlay toward Flutter/SDL paths that already exist.
3. Replace video overlay after the main game picture path is stable.
4. Only then remove `TVPWindowLayer`/Cocos `Director`/Cocos Activity ownership.

## Reference implementation constraints to keep

- AetherKiri:
  - Dirty writes are represented as region sets (`tTVPComplexRect`), not as
    full-frame updates on bind.
  - Swap/present is gated by dirty state; do not add unconditional swap.
- krkrsdl3:
  - OpenGL texture upload uses `glTexSubImage2D` with
    `GL_UNPACK_ROW_LENGTH = pitch / 4` and `GL_UNPACK_ALIGNMENT = 1`.
  - It is intentionally simple; do not add per-frame validation.
- LauncherC current matching points:
  - `tTVPOGLTexture2D::InternalUpdate()` already uses pitch-aware subrect
    upload when GL unpack row length is supported.
  - `SDLAndroidFlutterPresenter::EnsureAndroidEGLUploadTextureLocked()` already
    does pitch-aware Android EGL software uploads.
  - `GetTextureRegionUploadPointer()` avoids `uploadScratch` when scanline and
    row-length support are available.
  - The scratch fallback should remain only for unsupported row-length or
    unavailable scanline cases.

Avoid in hot paths:

- `glReadPixels` output verification.
- checksum/nonblank/alpha scan validation.
- verbose GL state dumps.
- SDL_GPU shadow upload while Android Flutter direct presenter is active.
- per-frame `glGetError()` outside diagnostics or resource creation/failure
  paths.

## 2026-07-04 10:51 log and continuation

Log inspected:

- `/root/log/20260704105124483.log`

Findings:

- No native crash stack was present in this log.
- Runtime is still launched through Cocos:
  - `runtimeHost=cocos2d StartGame(...) returned 1`
  - This confirms the hard migration target is still unfinished. Cocos is still
    the runtime host even though the game picture can be handed to SDL/Flutter.
- Android EGL presenter lifecycle:
  - Flutter surface starts at `1x1`, then resizes to `2780x1264`, then the game
    surface is recreated at `1920x1080`.
  - EGL presenter logs `surface dropped reason=context-change`, `recreate`,
    and later `surface ready #1/#2`.
  - `surface ready ... preserve=0`, so this device/session cannot safely rely
    on preserved backbuffer contents for partial swap.
- Consequence for presenter design:
  - Dirty rects should currently control whether a frame needs present/swap and
    should reduce CPU/GPU upload work.
  - Do not claim or depend on partial EGL swap while `preserve=0`.
  - When present is needed under `preserve=0`, submit a complete composed frame.
  - When GL-backed texture has no dirty region, the runtime presenter should
    return handled and skip the EGL swap, rather than forcing a full-frame
    `nativePresentOnly` draw every `Show()`.
- Input queue still shows pressure during script/render work:
  - `maxAgeMs=317`, `maxBacklog=15`, `dropped=1`, `coalesced=79`.
  - Reducing repeated no-change presents is still relevant to input latency.

Code continuation from this log:

- `iTVPTexture2D` gained optional complex dirty-region APIs:
  - `MarkDirtyRegion`
  - `PeekDirtyRegion`
  - `ConsumeDirtyRegion`
- Software static textures now store dirty state as `tTVPComplexRect` and keep
  `PeekDirtyRect`/`ConsumeDirtyRect` as bounding-rect compatibility wrappers.
- OpenGL textures now store dirty state as `tTVPComplexRect` with the same
  compatibility wrappers.
- The Cocos fallback adapter upload can consume a dirty region and call the
  existing pitch-aware rectangle uploader for each rect.
- `TVPSDLTryPresentTexture()` now treats no-dirty GL-backed frames under
  Android takeover as handled/no-op after the first presented frame, instead of
  converting them into full-frame EGL swaps. Surface recreate and explicit
  `forceFullFrame` still force full-frame present before this no-op gate.

Reference constraint from AetherKiri/krkrsdl2/krkrsdl3:

- AetherKiri validates frame-dirty-gated swap (`MarkFrameDirty` /
  `ConsumeFrameDirty`), not unconditional swap.
- krkrsdl2 validates `needsGraphicUpdate` present gating and pitch-aware CPU
  staging copies, but its dirty rect is only a union rect.
- krkrsdl3 validates SDL3 + GL + pitch-aware texture upload, but its main loop
  swaps every frame and should not be copied for Flutter/SDL3 embedding.
- Therefore LauncherC should continue with:
  - dirty-region tracking,
  - pitch-aware uploads,
  - full composed present when the Android surface is not preserved,
  - no-op present when no frame is dirty,
  - no hot-path graphical integrity validation.
