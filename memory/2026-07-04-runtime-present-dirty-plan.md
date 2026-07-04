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
