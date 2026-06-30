# 2026-06-29 SDL3/Flutter migration context

This file is intentionally detailed. If the chat context is compacted or lost,
read this file before editing. The user's hard requirement is not a temporary
fallback. The target is a complete, robust, high-performance migration of
KiriKiri-LauncherC to a Flutter shell plus SDL3/native runtime and presenter.

## Scope

- Only edit `/root/kiriki-work/KiriKiri-LauncherC`.
- Other folders under `/root/kiriki-work` are references only:
  `AetherKiri`, `docs`, `kirikiroid2-web`, `KrKr2-Next`,
  `krkrsdl2-main`, `krkrsdl3-main`, `SDL-release-3.4.10`,
  `KiriKiri-LauncherC-1.3`.
- `/root/kiriki-work/docs` is documentation/reference only. Do not write there.
- The local reference projects can be read for design and implementation
  patterns, but do not modify them.
- The project being changed is `KiriKiri-LauncherC`.

## User requirements and engineering policy

- Hard target: migrate toward `Flutter + SDL3`.
- Non-negotiable quality bar from the user: implementation must be complete,
  high-performance, and highly compatible. Do not accept a slow but convenient
  fallback as the final design when a stronger GPU/native path is available.
- Reference projects that must be consulted for architecture and performance
  decisions:
  - `AetherKiri`
  - `kirikiroid2-web`
  - `KrKr2-Next`
  - `krkrsdl2-main`
  - `krkrsdl3-main`
  - `SDL-release-3.4.10`
- When touching rendering, loading, presentation, texture upload, color upload,
  pixel conversion, or image decode paths, compare against those reference
  projects first. Prefer the design that minimizes CPU copies/readbacks,
  preserves compatibility fallback behavior, and fits the Flutter + SDL3
  migration target.
- When a reference project already has a stable implementation for a hard
  problem, prefer adapting or directly porting that proven approach over
  inventing a novel path. Avoid experimental "clever" designs unless the
  reference implementations cannot fit this project's Flutter + SDL3 host
  boundary. Stability, compatibility, and measured performance are more
  important than originality.
- The implementation should be as complete, robust, high-performance, and
  compatible as possible. Do not settle for a cosmetic fallback when a real
  presenter/runtime fix is required.
- Work incrementally. Do not delete Cocos in one large change; move ownership
  behind runtime/presenter boundaries until SDL3/Flutter paths reach parity.
- Keep current launcher behavior intact: game scanning, game root,
  custom launch file, per-game overrides, loading console, in-game menu, and
  diagnostics.
- Current stable Android presentation path is Flutter direct
  `ANativeWindow` dirty copy. This is not the final performance target, but it
  is the working bridge while SDL3/GPU/SurfaceTexture direct paths mature.
- Future high-performance target: native presenter under SDL3/RuntimeHost, with
  Flutter as shell/overlay and no per-frame Flutter UI involvement when no UI
  overlay needs updates.
- Prefer compatibility-first runtime selection:
  - auto-select SDL_GPU driver by default;
  - explicit driver only via diagnostics env var;
  - keep direct Flutter presentation alive if experimental GPU paths fail.
- If using credentials from old session logs, use them only for one process
  invocation. Do not write tokens to remotes, git config, environment files, or
  credential helpers.

## Verified baseline

- Commit `ff4844987d5db109e1fc09400e7aafb1b1baa300`
  (`Enable SDL GPU Vulkan diagnostics`) was pushed to `origin/main`.
- GitHub Actions for that commit:
  - `format-check`: success
  - `build-android`: success
- Local machine is missing `cmake`, `clang-format`, `java`, `flutter`, and
  `dart`; CI is the authoritative build check.
- Useful local checks that do work:
  - `git -C /root/kiriki-work/KiriKiri-LauncherC diff --check`
  - `python3 -m json.tool /root/kiriki-work/KiriKiri-LauncherC/vcpkg.json`

## Current log findings

Logs from the pushed build:

- `/root/log/78.log`
- `/root/log/20260629022307020.log`
- `/root/log/20260629022423320.log`
- `/root/log/20260629041311767.log`
- `/root/log/20260629041351243.log`

Important observations:

- Flutter direct `ANativeWindow` presentation works and stays alive:
  `present-flutter-direct` reaches frame `#1024` in
  `20260629022423320.log`.
- Cocos is hidden after the first successful Flutter direct present.
- SDL_GPU reports `available=vulkan`, but creating a device with forced
  `name="vulkan"` fails:
  `SDL_CreateGPUDevice failed: SDL_HINT_GPU_DRIVER vulkan unsupported!`
- SDL source in `/root/kiriki-work/SDL-release-3.4.10/src/gpu/SDL_gpu.c`
  shows this error comes from `SDL_GPUSelectBackend()` after an explicit
  driver name/hint fails `PrepareDriver()`.
- `SDL_GetGPUDriver()` enumeration only proves the backend is compiled/listed;
  it does not prove the current device can satisfy Vulkan setup.
- SDL 3.4.10 docs in `include/SDL3/SDL_gpu.h` say Android Vulkan
  compatibility can be improved by disabling optional feature requirements:
  clip distance, depth clamping, indirect draw first instance, and anisotropy.
- Therefore `graphics_backend=gpuapi` is not ready to become the default
  presenter on this Android device. It should remain diagnostic/shadow-upload
  until SDL_GPU device creation succeeds reliably, but the code should still
  actively improve SDL_GPU compatibility instead of giving up.
- Input diagnostics show queue pressure under rapid touch:
  `sdl-inputqueue dropped=64` and later `dropped=105`.
- Logs show `sdl-inputqueue` drops happen during heavy script/render work and
  rapid touch move bursts. Begin/end/cancel ordering must not be broken when
  this is optimized.
- New 04:13 logs after commits `36c9dcb` and `105c4f3` show real progress:
  - `sdl-gpu-presenter backend ready ... driver=vulkan`, so Android relaxed
    SDL_GPU device creation now works on the target device.
  - `present-flutter-direct` no longer stops at `#1`; logs show continuous
    direct presentation through at least `#256`.
  - The white/static first-frame failure is therefore mitigated by forcing
    continuous full-frame presentation for GL-backed textures.
  - Current bottleneck is performance: every Android takeover frame still does
    a 1920x1080 CPU readback/copy into Flutter's `ANativeWindow`, and with
    `graphics_backend=gpuapi` it also did a per-frame SDL_GPU shadow upload.
    Since no SDL_GPU swapchain consumes the uploaded texture yet, that shadow
    upload is diagnostic-only duplicate work.
  - Input latency still has spikes under heavy script/render work:
    `maxAgeMs=401` in `20260629041311767.log` and `maxAgeMs=549` in
    `20260629041351243.log`. Coalescing keeps backlog from growing
    permanently, but reducing per-frame duplicate work is the next practical
    performance fix.

## Reference project findings already known

- `KrKr2-Next`:
  - `apps/flutter_app/lib/constants/prefs_keys.dart`: renderer and backend are
    separate preferences.
  - `apps/flutter_app/lib/pages/game_page.dart`: renderer is selected before
    opening a game, backend separately.
  - `apps/flutter_app/lib/pages/settings_page.dart`: UI exposes renderer
    `opengl/software` and backend `opengles/vulkan`.
  - `bridge/engine_api/include/engine_options.h`: option constants.
- `krkrsdl3-main`:
  - `cpp/krkrsdl.cpp`: SDL3 callback lifecycle with `SDL_AppInit`,
    `SDL_AppEvent`, and `SDL_AppIterate`.
  - `cpp/krkrsdl_gl.cpp`: GLES/GL texture presentation path.
- `AetherKiri`:
  - `bridge/engine_api/src/android_jni_bridge.cpp`: stores `ANativeWindow`
    from SurfaceTexture.
  - `cpp/core/visual/ogl/krkr_egl_context.*`: attaches Android
    `ANativeWindow` as an EGL WindowSurface and swaps directly to a
    SurfaceTexture. This is the likely future zero-copy/low-copy Flutter
    texture path.
- `KiriKiri-LauncherC-1.3`:
  - useful for old behavior comparison, especially startupFrom and Android
    lifecycle comments, but it is older and should not be copied blindly.

## Current local work in progress

- RuntimeHost launch entry consolidation:
  - Added detailed launch status in `cpp/core/environ/runtime/RuntimeHost.*`.
  - Routed AppDelegate, file selector, and Flutter overlay launch requests
    through `TVPStartGameOnRuntimeHost*`.
- SDL_GPU compatibility work:
  - `cpp/core/render/sdlgpu/SDLGpuBackend.cpp` is being changed from
    `SDL_CreateGPUDevice` to `SDL_CreateGPUDeviceWithProperties`.
  - Android should default to SPIR-V only and relaxed Vulkan feature
    requirements:
    clip distance, depth clamping, indirect draw first instance, anisotropy.
  - `cpp/core/environ/sdl/SDLGameManager.cpp` should not force `vulkan` as the
    preferred SDL_GPU driver unless `KRKR2_SDL_GPU_DRIVER` is explicitly set.
- Documentation:
  - `docs/sdlgpu-render-plan.md` needs to record auto driver selection and
    relaxed Android Vulkan feature requirements.
  - `docs/runtime-host-architecture.md` records that file selector and Flutter
    overlay launch requests now go through RuntimeHost.
- Current uncommitted files may include:
  - `cpp/core/environ/cocos2d/AppDelegate.cpp`
  - `cpp/core/environ/runtime/RuntimeHost.cpp`
  - `cpp/core/environ/runtime/RuntimeHost.h`
  - `cpp/core/environ/sdl/SDLGameManager.cpp`
  - `cpp/core/environ/ui/FlutterGameMenuBridge.cpp`
  - `cpp/core/environ/ui/MainFileSelectorForm.cpp`
  - `cpp/core/render/sdlgpu/SDLGpuBackend.cpp`
  - `docs/runtime-host-architecture.md`
  - `docs/sdlgpu-render-plan.md`
  - `memory/2026-06-29-sdl3-flutter-migration.md`
- Input queue performance work added in `SDLGameManager.cpp`:
  - `gSDLInputCoalesced` tracks high-frequency events consumed by coalescing.
  - Backlog calculation now subtracts drained + dropped + coalesced.
  - `TVPSDLProcessAndroidInputQueue()` coalesces contiguous direct
    `touch-move` events for the same pointer and dispatches only the latest
    position.
  - Follow-up after subagent review: do not use `SDL_PEEKEVENT` to inspect a
    `TVPSDLQueuedInputEvent *` before owning it. The processor now drains the
    current SDL custom-event batch into a local `std::deque` under
    `gSDLInputQueueMutex`, then coalesces adjacent move pointers from that
    owned local batch. `DropQueuedAndroidInputEvents()` uses the same mutex.
    This avoids a lifecycle drain deleting a pointer that coalescing only
    peeked.
  - During queue processing after the first frame has presented, stale
    direct-touch dropping now applies to `touch-move` only; begin/end and
    cancel are preserved to avoid breaking click/tap ordering under load.
  - Follow-up after subagent review: when a stale direct `touch-move` is
    dropped for the active pointer, `CancelSDLDirectTouchForPointer()` resets
    the direct-touch state. This prevents a delayed `touch-end` from turning a
    stale drag into a synthetic click.
  - Pre-first-frame direct-touch events are still dropped before queueing when
    no presenter has produced a frame. This avoids replaying stale touches into
    a game surface that did not exist yet.
  - Queue logs include `coalesced=...`.
- SDL_GPU diagnostics work added in `SDLGpuBackend.cpp`:
  - Failure from `SDL_CreateGPUDeviceWithProperties` now appends
    `driver=...`, `shaders=...`, and `features=strict|relaxed` to the error.
  - Follow-up after subagent review: normal initialization no longer sets
    `SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN` to false. SDL's default verbose
    creation diagnostics are preserved; debug mode still explicitly enables
    verbose logging.
  - Follow-up after subagent review: when `KRKR2_SDL_GPU_DRIVER` explicitly
    selects a driver, `SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, ...,`
    `SDL_HINT_OVERRIDE)` is set before device creation so an external
    `SDL_GPU_DRIVER` hint cannot silently override the project-specific
    diagnostic knob.
  - Follow-up after 04:13 device logs: `graphics_backend=gpuapi` now means
    SDL_GPU device initialization and diagnostics by default. On Android, while
    the active presenter is still Flutter direct `ANativeWindow`, per-frame
    SDL_GPU shadow uploads are skipped unless
    `KRKR2_ENABLE_SDL_GPU_SHADOW_UPLOAD=1` is explicitly set. This avoids
    double-paying CPU readback/copy plus Vulkan upload before a real
    SDL_GPU/SurfaceTexture presenter consumes the GPU texture.
  - This should make the next device log useful without rebuilding a special
    diagnostic branch.
- Flutter launcher documentation:
  - `docs/flutter-launcher.md` was updated so
    `KR2LauncherLaunchGame(const char*)` is documented as going through the
    active `RuntimeHost` launch path instead of directly through
    `TVPMainScene::startupFrom`.
  - Follow-up after subagent review: null or empty
    `KR2LauncherLaunchGame()` paths now still call
    `TVPStartGameOnRuntimeHostDetailed()` with an empty path so the
    centralized `runtime-host` diagnostic records the `EmptyPath` rejection.

## Recommended direction

- Keep Flutter direct `ANativeWindow` presentation as the stable Android path
  for now because logs prove it works over sustained frames.
- Treat SDL_GPU as shadow-upload/diagnostic until a real device is created on
  target Android hardware.
- Make GPU backend selection compatibility-first:
  auto driver by default, explicit driver only via environment override.
- Do not force `preferredDriver="vulkan"` for `gpuapi` or `graphics_backend`
  `vulkan`. Forced driver selection can turn a compiled-but-not-supported
  backend into immediate failure.
- Use `SDL_CreateGPUDeviceWithProperties` so Android can request SPIR-V only
  and relax optional Vulkan feature requirements by default. Keep
  `KRKR2_SDL_GPU_STRICT_FEATURES=1` as a diagnostic escape hatch.
- Continue moving launch/input/presentation ownership behind `RuntimeHost`
  without deleting Cocos in one large change.
- Next performance targets:
  - reduce Flutter/SDL input queue drops by coalescing move events while never
    dropping begin/end/cancel ordering;
  - validate the opt-in AetherKiri-style EGL attachment to Flutter
    `SurfaceTexture` as the next low-copy path.

## Current EGL/SurfaceTexture experiment

This work is intentionally opt-in. The stable Android presenter is still the
direct Flutter `ANativeWindow_lock` CPU copy path.

New implementation in progress:

- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Includes Android EGL/GLES2 headers under `__ANDROID__`.
  - Adds `TVPAndroidEGLSurfacePresenterState` guarded by
    `gSDLAndroidEGLPresenterMutex`.
  - Adds `TryPresentAndroidEGLSurfaceTexture()` before the existing
    `TryPresentAndroidFlutterTexture()` fallback in `TVPSDLTryPresentTexture()`.
  - Enables the EGL path only when
    `KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT=1`.
  - The path acquires the existing Flutter `ANativeWindow` through
    `TVPAndroidAcquireFlutterGameSurfaceWindow()`, discovers the current EGL
    display/context/config from the Cocos render thread, creates an EGL
    `WindowSurface` for the Flutter `SurfaceTexture`, temporarily makes that
    surface current with the current context, draws a full-frame GLES2 quad from
    `iTVPTexture2D::GetNativeGLTextureId()`, flushes, swaps with
    `eglSwapBuffers`, then restores the previous EGL draw/read surfaces and GL
    state.
  - It calls `TVPSetRenderTarget(0)` before sampling a native GL texture so the
    final TVP texture is not still attached to the engine FBO.
  - It computes UV scale from `GetWidth()/GetInternalWidth()` and
    `GetHeight()/GetInternalHeight()` so power-of-two internal textures sample
    only the logical frame.
  - It does not partial-present dirty rectangles. A successful EGL present is
    always logged as `present-texture-egl ... fullFrame=1`; dirty state only
    determines whether a new present is needed.
  - It does not upload software textures by default. If
    `GetNativeGLTextureId() == 0`, the path returns false and the stable CPU
    presenter handles the frame. Set
    `KRKR2_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE=1` only for diagnostics; that
    copies scanlines into a scratch buffer and uploads one RGBA GL texture.
  - Set `KRKR2_ANDROID_EGL_SURFACE_FLIP_Y=1` only if device evidence shows the
    Flutter external texture is vertically inverted. AetherKiri's Android
    SurfaceTexture path does not flip by default.
  - Logs use the `android-egl-presenter` tag:
    `surface ready ...`, `program ready ...`, `present-android-egl ...`, and
    `failure ...`.
  - With `showfps`, overlay renderer info appends `androidEgl=...` counters
    when the flag is enabled.
  - Any EGL/program/draw/swap failure returns false so the existing direct CPU
    presenter remains the visible fallback. If restoring the original EGL
    current surface fails, the path marks itself fatal and reports the restore
    error.
- `cpp/core/environ/sdl/SDLGameManager.h`
  - Adds Android-only C ABI
    `TVPSDLNotifyAndroidFlutterGameSurfaceChanged(const char *reason)`.
- `platforms/android/cpp/krkr2_android.cpp`
  - `nativeSetGameSurface`, `nativeResizeGameSurface`, and
    `nativeDetachGameSurface` now notify the SDL presenter after updating the
    stored Flutter `ANativeWindow`.
  - The notification drops cached EGL window-surface resources so the render
    thread recreates them on the next present. This is necessary because Dart
    route disposal is unawaited and Java may release the `SurfaceTextureEntry`
    while frames are still in flight.
- `docs/sdlgpu-render-plan.md`
  - Documents the opt-in EGL/SurfaceTexture path and diagnostics env vars.
- `docs/runtime-host-architecture.md`
  - Notes that the EGL path is a controlled experiment, not the final default
    SDL3 host.

Why this path was chosen:

- New device logs proved SDL_GPU can create a Vulkan device, but no SDL_GPU
  swapchain consumes the final texture yet. Vulkan is therefore not the next
  presenter step.
- `MainActivity.createGameSurfaceTexture()` already creates the correct Flutter
  external texture and passes a `Surface` to native. Reusing that bridge keeps
  the Dart `Texture` widget, touch mapping, metrics polling, and Cocos-hide
  behavior intact.
- AetherKiri uses the same conceptual model:
  `SurfaceTexture -> Surface -> ANativeWindow -> EGL WindowSurface ->
  full-frame GL blit -> eglSwapBuffers`.
- SDL3's stock Android backend still obtains its native window through
  SDLActivity/SDLSurface, so making SDL3 own the Flutter external texture would
  require a vendored SDL Android backend patch. The current manual EGL bridge is
  the lower-risk first step while the project still uses the Cocos GL thread.

Validation required on device before making EGL default:

- `android-egl-presenter surface ready` appears after the Flutter texture is
  created.
- `present-android-egl` reaches high frame counts, not just frame 1.
- `present-flutter-direct` stops or becomes fallback-only when the EGL flag is
  enabled and EGL succeeds.
- Screen is nonblank, not vertically inverted, and not stale/double-buffer
  flickering.
- Touch begin/move/end still maps to the game surface correctly.
- Resize/dispose does not crash; logs should show surface dropped/recreated.
- Input queue `maxAgeMs` should improve compared with full-frame CPU copy logs.

Known risks:

- This borrows the current Cocos EGL context. GL state is saved/restored, but
  device logs are still required to catch vendor-specific state-cache issues.
- EGL surface creation can fail with `EGL_BAD_MATCH` if the current config is
  incompatible with the Flutter `ANativeWindow`; fallback should keep rendering.
- If the final texture is not GL-backed, the default EGL path intentionally does
  nothing and lets the CPU presenter handle the frame.

## Subagent status

The user requested 2-6 subagents for assistance. Four earlier read-only
subagents were spawned, but all failed due provider/key/limit errors:

- Surface/presenter reference analysis: failed with provider 503.
- SDL_GPU compatibility analysis: failed with provider 503.
- SDL input queue analysis: failed with provider 503.
- Flutter/SurfaceTexture comparison: failed with provider parameter/key error.

Do not wait for those failed agents. Later in this run, three new read-only
explorers were started and returned:

- `019f0fbd-9482-7b00-8e9a-98a708a07536` / Planck: input queue coalescing
  review. Important findings: high-risk `SDL_PEEKEVENT` use-before-ownership
  with concurrent lifecycle drain; medium-risk stale move drop leaving
  direct-touch state active. Both were fixed in the follow-up patch described
  above.
- `019f0fbd-9525-7fc3-a4cf-5c2c08e228bf` / Hubble: SDL_GPU compatibility
  review. Important findings: stale move direct-touch reset regression,
  suppressed SDL_GPU verbose diagnostics, external `SDL_GPU_DRIVER` hint
  priority. These were fixed or documented as described above. It also noted
  that `available=` GPU drivers are compiled candidates, not proof that
  `PrepareDriver()` passed.
- `019f0fbd-95b4-7d51-82b5-4b71d87fcfcf` / Kuhn: RuntimeHost launch
  migration review. Important finding: empty-string C ABI launches bypassed
  centralized diagnostics. Fixed by routing null/empty paths through
  `TVPStartGameOnRuntimeHostDetailed()`.

## Next concrete steps

1. Finish local checks for the EGL/SurfaceTexture experiment:
   - `git diff --check`;
   - `python3 -m json.tool vcpkg.json`;
   - grep changed symbols for obvious declaration/order mistakes;
   - rely on CI for Android compile because local `cmake`, `java`, `flutter`,
     and `clang-format` are unavailable.
2. Commit with a message like
   `Add opt-in Android EGL SurfaceTexture presenter`.
3. Push to `origin/main` using one-shot credential input if needed. Do not
   store the PAT.
4. Monitor GitHub Actions for the new commit. Fix compile/format failures
   immediately.
5. Ask for or inspect new device logs with:
   - default flags, to ensure fallback behavior is unchanged;
   - `KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT=1`, to validate the EGL path;
   - optionally `KRKR2_ANDROID_EGL_SURFACE_FLIP_Y=1` if output is inverted.

## 2026-06-30 continuation checkpoint

User asked to continue after new logs:

- `/root/log/78.log`
- `/root/log/20260629041311767.log`
- `/root/log/20260629041351243.log`

Repository state at the start of this checkpoint:

- Working tree: `/root/kiriki-work/KiriKiri-LauncherC`
- Branch: `main`
- Local/remote commit before changes:
  `2324833 Add opt-in Android EGL SurfaceTexture presenter`
- `main` was synced with `origin/main`.
- Only this project may be edited. Other `/root/kiriki-work/*` projects remain
  reference-only.

GitHub Actions failure investigated:

- Failed workflow/job from commit `2324833`:
  - Build Flutter Android run: `28336119071`
  - Job: `83942523641`
  - Conclusion: failure
- The job logs endpoint returned a plain text log body, not a zip archive. The
  log was saved temporarily as
  `/tmp/kiriki-ci-logs/job-83942523641.zip`, but it is text despite the name.
- Exact compile failure:
  - `cpp/core/environ/sdl/SDLGameManager.cpp:1301:9`
  - `error: use of undeclared identifier 'RememberPresentedSurfaceSize'; did
    you mean 'TVPSDLGetPresentedSurfaceSize'?`
  - Follow-up diagnostic:
    `cannot initialize a parameter of type 'int *' with an lvalue of type
    'int'`
- Root cause:
  - `TryPresentAndroidEGLSurfaceTexture()` calls
    `RememberPresentedSurfaceSize(surfaceWidth, surfaceHeight)` before the
    helper's definition.
  - The helper was defined later in the Android presenter block and had no
    forward declaration.
  - The older CPU presenter paths call the helper after its definition, so this
    only surfaced when the new EGL path was compiled.
- Fix applied in `cpp/core/environ/sdl/SDLGameManager.cpp`:
  - Added an Android-only anonymous-namespace forward declaration:
    `void RememberPresentedSurfaceSize(int width, int height);`
  - Kept the helper definition in place to avoid moving large presenter blocks
    or changing ABI-visible code.

Extra robustness applied while fixing the CI break:

- The Android EGL blit presenter already saves/restores framebuffer, viewport,
  active texture, texture binding, array buffer, program, blend/depth/scissor,
  and vertex attributes.
- The software diagnostic upload path calls `glPixelStorei(GL_UNPACK_ALIGNMENT,
  1)`, and the draw path sets `glClearColor(...)`.
- Added `unpackAlignment` and `clearColor` to `TVPAndroidGLStateSnapshot`, then
  save/restore them in `SaveAndroidGLState()` and
  `RestoreAndroidGLState()`.
- This prevents the opt-in EGL presenter from leaking pixel-store or clear
  color state back into the Cocos/TVP render path after presenting.

Local checks run after the patch:

- `git diff --check`: passed.
- `python3 -m json.tool vcpkg.json`: passed.
- Manual line-length scan found only pre-existing long lines in
  `SDLGameManager.cpp`; the new lines are within the existing formatting style.
- Local Android compile is still not possible in this environment because
  `cmake`, `java`, `flutter`, `dart`, and `clang-format` are unavailable.
  GitHub Actions remains authoritative for Android build and format.

New device/application log inspection:

- `20260629041311767.log`:
  - `android-egl-presenter` occurrences: `0`
  - `present-flutter-direct` occurrences: `13`
  - `present-texture-direct` occurrences: `13`
  - core error/critical/fatal/SIGSEGV matches: `0`
- `20260629041351243.log`:
  - `android-egl-presenter` occurrences: `0`
  - `present-flutter-direct` occurrences: `13`
  - `present-texture-direct` occurrences: `13`
  - core error/critical/fatal/SIGSEGV matches: `0`
- Interpretation:
  - These logs appear to be from the previous build or default flags, not from
    the opt-in EGL path.
  - The stable Flutter `ANativeWindow_lock` CPU presenter fallback is still
    working at 1920x1080.
  - SDL_GPU/Vulkan diagnostic backend still initializes (`available=vulkan`,
    `backend ready ... driver=vulkan`) and the final frame still presents via
    the Flutter external texture CPU path.

Subagents started in this continuation:

- Existing agent `019f0fe1-aaf2-7611-ba5a-9e0e1793e5b1` was asked to inspect
  current Android EGL presenter compile/NDK/GLES/EGL hazards.
- Existing agent `019f0fec-8657-70c3-8038-b60c3057470d` was asked to compare
  SDL3/reference project Surface/EGL implementation details.
- Existing agent `019f0fed-315d-7c62-b5b5-67850638c9fa` was asked to locate
  the CI failure or infer likely failure points.
- Do not block on them unless their output is needed. The main CI failure was
  already confirmed directly from the GitHub Actions log.

Next concrete steps from here:

1. Commit the current fix with a message like
   `Fix Android EGL presenter compile guard`.
2. Push to `origin/main` using one-shot credential input only if required.
3. Monitor new GitHub Actions runs until both `format-check` and
   `build-android` complete.
4. If CI reveals another compile issue, fetch the exact log and patch only
   `KiriKiri-LauncherC`.
5. After CI passes, request/test a build with
   `KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT=1` to gather real
   `android-egl-presenter` runtime logs.

## 2026-06-30 render performance continuation

User clarified that the performance concern is not ordinary logging, but
graphics-integrity style validation/probing: paths that touch texture pixels,
read scanlines, or force GPU-backed textures into CPU readback.

Important finding:

- Android screen takeover previously set
  `gSDLSurfaceMirrorConsumerActive = true`.
- That made every bitmap completion region run
  `CopyRegionToSDLSurfaceMirror()`.
- `CopyRegionToSDLSurfaceMirror()` copies each row through
  `texture->GetScanLineForRead(...)`.
- For OpenGL-backed TVP textures, `GetScanLineForRead()` in
  `RenderManager_ogl.cpp` can allocate a CPU pixel cache and call
  `glReadPixels()` for the whole texture. This is exactly the kind of
  graphics-integrity/readback work that hurts Android frame time.
- The actual Flutter direct presenter does not need the SDL surface mirror:
  `BasicDrawDevice::Show()` still calls `UpdateDrawBuffer(tex)`, which reaches
  `TVPSDLTryPresentTexture()` and then the Android Flutter direct presenter.

Patch applied:

- `IsSDLRenderDiagnosticsActive()` now only honors the explicit
  `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS` flag.
  - Selecting `graphics_backend=gpuapi` or enabling SDL_GPU no longer
    automatically turns on render diagnostics.
- Added `ShouldUseSDLSurfaceMirrorForTakeover()`.
  - On Android it defaults to false.
  - Set `KRKR2_ENABLE_ANDROID_SDL_SURFACE_MIRROR=1` only for diagnostics or
    legacy fallback investigation.
  - Non-Android behavior remains true so desktop SDL window/surface presenter
    behavior is not changed by this Android performance fix.
- `TVPSDLSetScreenTakeoverEnabled()` now logs `surfaceMirror=0/1` and only
  keeps the mirror active when the new helper allows it.
- `TVPSDLRecordBitmapCompletionStart/Region/End()` now return immediately when
  both diagnostics and surface mirror are disabled.
  - When diagnostics are disabled but the surface mirror is explicitly enabled,
    they still maintain the mirror and pump the surface presenter.
  - When only diagnostics are enabled, they still produce the previous
    `sdl-bitmap` probe logs.
- Updated docs:
  - `docs/runtime-host-architecture.md`
  - `docs/sdlgpu-render-plan.md`

Why this matches the Flutter + SDL3 migration goal:

- Default Android presentation now prefers the Flutter external texture direct
  path and avoids an extra per-region CPU mirror.
- This keeps the stable fallback available behind an environment flag while the
  low-copy EGL/SurfaceTexture path matures.
- It moves the project away from Cocos-era bitmap completion mirroring and
  toward a presenter boundary that can be owned by SDL3/Flutter.

Reference notes gathered this turn:

- `krkrsdl3-main/cpp/krkrsdl.cpp` uses SDL3 callback entry points
  `SDL_AppInit`, `SDL_AppEvent`, and `SDL_AppIterate`.
- `krkrsdl3-main/cpp/environ/MainWindowLayer.cpp` forwards update/event work
  through `SDL_AppIterate()` and `SDL_AppEvent()`, showing the shape of an
  SDL-owned frame loop without Cocos.
- `AetherKiri/bridge/engine_api/src/android_jni_bridge.cpp` stores an
  `ANativeWindow` from a SurfaceTexture/Surface bridge.
- `AetherKiri/bridge/engine_api/src/engine_api.cpp` auto-attaches pending
  `ANativeWindow` instances during `engine_tick()`.
- `AetherKiri/cpp/core/visual/ogl/krkr_egl_context.cpp` creates and attaches
  EGL `WindowSurface` objects from `ANativeWindow`, then uses
  `eglSwapBuffers()` to deliver frames to the SurfaceTexture. This reinforces
  the current direction: prefer SurfaceTexture/EGL presenter work over CPU
  bitmap/surface mirrors.

Checks:

- `git diff --check`: passed.
- `python3 -m json.tool vcpkg.json`: passed.
- Local full Android build still unavailable here because `cmake`, `java`,
  `flutter`, `dart`, and `clang-format` are absent; GitHub Actions is still
  authoritative.

## 2026-06-30 Cocos dependency reduction slice

After pushing `0a660f0`, user reiterated that the hard goal is still
Flutter + SDL3 and that Cocos should be removed gradually.

Small low-risk code slice applied:

- `TVPMainScene` now exposes `GetRuntimeFrameMetrics()`.
- `CocosRuntimeHost::GetFrameMetrics()` no longer reads
  `cocos2d::Director::getInstance()->getOpenGLView()` directly.
- The Cocos-specific frame/scene size calculation is now localized inside
  `MainScene.cpp`; `CocosRuntimeHost.cpp` simply returns
  `scene->GetRuntimeFrameMetrics()`.
- This keeps behavior unchanged while narrowing the runtime-host boundary for a
  future SDL3 host.

Why this helps remove Cocos:

- `runtime/RuntimeHost` should be the stable engine-host boundary used by both
  the current Cocos host and the future SDL3 host.
- Host-independent code should ask the active host for frame metrics instead of
  importing Cocos APIs or assuming a Cocos GLView.
- This is a small step toward matching `krkrsdl3-main`, where SDL owns the app
  callbacks (`SDL_AppInit`, `SDL_AppEvent`, `SDL_AppIterate`) and frame loop.

Next likely Cocos-removal slices:

1. Add a minimal SDL runtime-host skeleton that can report frame metrics and
   accept launch requests, behind an opt-in build/runtime flag.

## 2026-06-30 continuation after frame metrics push

User asked to keep comparing reference projects, push previous work, and
continue gradually removing Cocos.

Pushed work:

- Local commit `10e976c Localize Cocos frame metrics in MainScene` was pushed
  to `origin/main`.
- The direct unauthenticated `git push origin main` failed because the local
  environment had no interactive GitHub username/password prompt.
- The GitHub PAT was extracted from the old Codex session log only inside a
  one-shot shell process and used in the push URL. The token was not stored in
  git config or any repository file.
- GitHub Actions after push:
  - `Code Format Check` run `28401184288`: completed successfully.
  - `Build Flutter Android` run `28401220002`: in progress when this section
    was written.

Subagents started in this continuation:

- `019f151c-4308-7d03-a5e1-de726a9a015e` / Socrates:
  - Read-only SDL3 runtime-host/reference-project comparison.
  - Key conclusion: do not copy `krkrsdl3-main`'s
    `SDL_AppInit`/`SDL_AppEvent`/`SDL_AppIterate` window-owning callback model
    directly into the first Flutter migration slice.
  - KiriKiri-LauncherC's existing `iTVPRuntimeHost` shape is closer to the
    desired Flutter-driven host model: Flutter owns the SurfaceTexture/window,
    while native C++ owns `StartGame`, `RunFrame`, input queue draining, and
    presenter pumping.
  - Recommended first SDL3 host skeleton:
    `GetHostName() = "flutter-sdl3"`, `StartGame()` initially reuses the
    existing startup logic, and `RunFrame()` does `Application->Run()`,
    texture recycling, and `TVPSDLPumpScreenPresenter()`.
  - Important warning: do not let SDL3 create a separate real window in this
    first slice, because Flutter already owns the Android surface lifecycle.
- `019f151c-6b57-7b72-9569-159dfdcc2802` / Poincare:
  - Read-only Cocos dependency inventory.
  - Lowest-risk Cocos-removal targets identified:
    `cpp/core/base/SysInitIntf.cpp`, `PreferenceDefaults.cpp`,
    `IndividualConfigManager.cpp`, and `LocaleConfigManager.cpp`.
  - It independently recommended replacing
    `IndividualConfigManager.cpp`'s `cocos2d::FileUtils::isFileExist` with a
    thin project/platform file-exists helper.
  - It also recommended splitting `LocaleConfigManager` so the core text table
    and resource loading are not tied to Cocos UI widgets. This continuation
    performs the first half: resource/file loading no longer uses Cocos; the
    old `initText(cocos2d::ui::Text/Button*)` overloads remain for legacy UI
    compatibility.
- `019f151c-959d-7a32-ab71-56c72458b6a9` / Bernoulli:
  - Started to inspect Android/Flutter render bridge readback and redundant
    upload risks. Do not block on it unless its final result is needed.

Current uncommitted Cocos-removal slice:

- New files:
  - `cpp/core/environ/ConfigManager/ConfigFileIO.h`
  - `cpp/core/environ/ConfigManager/ConfigFileIO.cpp`
- `ConfigFileIO` provides:
  - `TVPConfigFileExists(path)` using standard `fopen`.
  - `TVPLoadConfigFileText(path, text)` using standard `fopen`/`fread`.
  - `TVPLoadBundledConfigText(logicalPath, text, resolvedPath)` using SDL3
    `SDL_LoadFile` first and stdio fallback.
  - Bundled resource lookup tries logical paths as packaged in Android assets
    (`locale/en_us.xml`), desktop source-tree paths
    (`ui/cocos-studio/locale/en_us.xml`), Flutter copied asset paths
    (`flutter_launcher/assets/cocos-studio/locale/en_us.xml`), and base-path
    prefixed variants.
- `cpp/core/environ/CMakeLists.txt` now builds `ConfigFileIO.cpp`.
- `cpp/core/environ/ConfigManager/IndividualConfigManager.cpp`:
  - Removed `platform/CCFileUtils.h`.
  - `CheckExistAt()` and `UsePreferenceAt()` now call
    `TVPConfigFileExists()`.
- `cpp/core/environ/ConfigManager/LocaleConfigManager.cpp`:
  - Removed `platform/CCFileUtils.h`.
  - Replaced Cocos `isFileExist`, `fullPathForFilename`, and
    `getStringFromFile` usage with `TVPLoadBundledConfigText()`.
  - Keeps fallback to `en_us` when the selected language XML cannot be loaded.
- `cpp/core/environ/ConfigManager/LocaleConfigManager.h`:
  - Renamed the private path helper to `GetLogicalFilePath() const`.
  - Still forward-declares `cocos2d::ui::Text/Button` because legacy Cocos UI
    text binding overloads remain in place for now.

Why this slice matters:

- A future `flutter-sdl3` host must be able to initialize preferences and
  locale text without constructing Cocos `FileUtils`.
- This slice does not touch the old Cocos scene tree, forms, or input behavior,
  so it is a low-risk boundary cleanup.
- It also avoids adding any render-path validation or pixel readback work; the
  performance fix from `0a660f0` remains intact.

Checks run for this slice:

- `git diff --check`: passed.
- `python3 -m json.tool vcpkg.json`: passed.
- Grep confirmed no `CCFileUtils`, `cocos2d::FileUtils`,
  `getStringFromFile`, `fullPathForFilename`, or `isFileExist` remain under
  `cpp/core/environ/ConfigManager`.
- Local full compile remains unavailable because this machine has no `cmake`,
  `java`, `flutter`, `dart`, or `clang-format`. GitHub Actions must verify the
  Android build.

Recommended next steps:

1. Recheck GitHub Actions run `28401220002` for pushed commit `10e976c`.
2. If that CI is green, commit the current ConfigFileIO slice with a message
   like `Remove Cocos file IO from config managers`.
3. Push and monitor format + Android build.
4. Next low-risk slices after this:
   - replace Cocos platform macros in `cpp/core/base/SysInitIntf.cpp`;
   - replace Cocos platform macros in `PreferenceDefaults.cpp`;
   - add an opt-in `flutter-sdl3` runtime-host skeleton without SDL window
     ownership.

## 2026-06-30 additional platform-macro cleanup

After committing and pushing the ConfigFileIO slice:

- Commit `82b3c9f Remove Cocos file IO from config managers` was pushed to
  `origin/main`.
- GitHub Actions:
  - `Code Format Check` run `28401800595`: completed successfully.
  - `Build Flutter Android` run `28401838181`: in progress when this section
    was written.
- Previous commit `10e976c` Android build run `28401220002` completed
  successfully.

Second low-risk Cocos-removal slice applied locally:

- `cpp/core/base/SysInitIntf.cpp`
  - Removed use of `CC_TARGET_PLATFORM`, `CC_PLATFORM_WIN32`, and
    `CC_TARGET_OS_IPHONE`.
  - Added local Apple `TargetConditionals.h` handling and a translation-unit
    macro `TVP_CORE_PLATFORM_IOS`.
  - Windows detection now uses `_WIN32`.
  - This removes the only direct Cocos platform-macro dependency under
    `cpp/core/base`.
- `cpp/core/environ/ConfigManager/PreferenceDefaults.cpp`
  - Removed use of `CC_TARGET_OS_IPHONE`, `CC_TARGET_PLATFORM`, and
    `CC_PLATFORM_ANDROID`.
  - Added local Apple `TargetConditionals.h` handling and
    `TVP_CONFIG_PLATFORM_IOS`.
  - Android defaults now use only `__ANDROID__` / `ANDROID`.
  - Default behavior remains the same: iOS keeps `memusage=high` and
    `GL_EXT_shader_framebuffer_fetch=true`; Android still gets
    `hide_android_sys_btn`, `ffmpeg_image_decoder`, and
    `ffmpeg_decode_mode` defaults.

Checks for this second slice:

- `git diff --check`: passed.
- `python3 -m json.tool vcpkg.json`: passed.
- Grep confirmed no `CC_TARGET_PLATFORM`, `CC_PLATFORM_`, or
  `CC_TARGET_OS_IPHONE` remain under `cpp/core/base` or
  `cpp/core/environ/ConfigManager`.

Why this matters:

- `cpp/core/base` is now free of direct Cocos platform macros, which is a
  prerequisite for building more of the engine under a future SDL3/Flutter host
  without dragging Cocos headers/macros through base initialization.
- `PreferenceDefaults` is still shared by the legacy Cocos host and future
  Flutter/SDL3 host, so its platform policy should not depend on Cocos.

## 2026-06-30 Android render performance push

New user feedback:

- Runtime performance still feels lower than expected.
- The user explicitly asked to study `AetherKiri`, `kirikiroid2-web`,
  `KrKr2-Next`, `krkrsdl2-main`, `krkrsdl3-main`, and `SDL-release-3.4.10`
  for rendering, upload, loading, presentation, texture upload, color upload,
  and GPU API design.
- The bottom-line requirement is complete implementation, high performance,
  and high compatibility.

New log inspected:

- `/root/log/20260630045249943.log`
- Key counts:
  - `android-egl-presenter`: 0
  - `present-android-egl`: 0
  - `present-flutter-direct`: 13
  - `present-texture-direct`: 13
  - `surfaceMirror=0`: present
  - `sdl-bitmap`: 0
  - fatal/critical/SIGSEGV: 0
- Interpretation:
  - The previous surface-mirror/readback fix is working: Android no longer
    enables the SDL surface mirror by default and bitmap completion diagnostics
    are not forcing per-region scanline reads.
  - The device still uses the CPU Flutter direct path:
    `ANativeWindow_lock` + `CopyTextureToAndroidBuffer`.
  - The EGL/SurfaceTexture path was not entered at all because it was still
    behind `KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT=1`.
  - Input queue `maxSeenAgeMs` reached 325 ms. This is likely tied to render
    thread stalls from CPU copy/readback; fix presentation first before more
    input-queue tuning.

Subagent/reference finding from Mill:

- The current bridge already matches the high-performance shape used by
  AetherKiri/KrKr2-Next:
  Flutter `SurfaceTextureEntry` -> Java `Surface` -> JNI `ANativeWindow` ->
  native EGL `WindowSurface` -> `eglSwapBuffers`.
- The missing piece is default selection:
  - Android default renderer was still `software`.
  - A software TVP texture has no `GetNativeGLTextureId()`.
  - Without a native GL texture, `TryPresentAndroidEGLSurfaceTexture()` cannot
    zero/low-copy present and falls back to CPU.
- SDL_GPU should not be treated as the immediate zero-copy Android solution
  yet. Current SDL_GPU upload still reads TVP scanlines and uploads through
  `SDL_UploadToGPUTexture`; it is useful for future renderer work but not a
  Flutter SurfaceTexture presenter today.

Local patch in progress:

- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Android EGL/SurfaceTexture presenter is now enabled by default.
  - `KRKR2_DISABLE_ANDROID_EGL_SURFACE_PRESENT=1` disables it.
  - Existing `KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT=1` still forces
    diagnostics and prevents auto-disable.
  - Added `autoDisabled` state. If EGL fails repeatedly before any successful
    EGL present, it auto-disables for that surface generation and falls back to
    the compatible CPU presenter to avoid retry overhead.
  - A Flutter surface resize/set/detach notification clears non-fatal
    auto-disable state so a recreated SurfaceTexture can be retried.
  - `TryPresentAndroidEGLSurfaceTexture()` now checks
    `GetNativeGLTextureId()` before acquiring the Flutter `ANativeWindow`, so
    software textures do not pay native-window overhead unless explicit
    software upload diagnostics are enabled.
  - `present-flutter-direct` logs now append `glBacked=0/1`, so new logs can
    distinguish software fallback from GL texture readback fallback.
  - Overlay `androidEgl=` can report `autoDisabled reason=...`.
- `cpp/core/environ/ConfigManager/PreferenceDefaults.cpp`
  - Android default renderer is now `opengl`; non-Android default remains
    `software`.
- `platforms/android/app/java/org/github/krkr2/KrkrPrefsSchema.kt`
  - Launcher preference schema default for `renderer` is now `opengl`.
- `platforms/android/app/java/org/github/krkr2/LauncherPrefs.kt`
  - Empty/unknown renderer preferences normalize to `opengl` instead of
    `software`.
- `platforms/android/app/java/org/github/krkr2/KR2Application.kt`
  - Added one-time bootstrap `engine_defaults_v5_opengl_renderer_applied`.
  - If global renderer is missing or still the old default `software`, it seeds
    `renderer=opengl`.
  - Users can still switch back to software later; this migration is intended
    to move existing old-default installs onto the high-performance path.
- `flutter_launcher/lib/src/bridge/launcher_bridge.dart`
  - Desktop/missing-plugin fallback engine settings now default to
    `renderer=opengl`.
- `flutter_launcher/lib/src/pages/launcher_home_page.dart`
  - Settings UI fallback for renderer now shows OpenGL.
- `flutter_launcher/lib/src/pages/game_overlay_page.dart`
  - Initial game SurfaceTexture size no longer hardcodes `1920x1080`.
  - Before native frame metrics are available, it uses the current Flutter
    layout constraints times device pixel ratio. This reduces unnecessary
    scaling/copy cost and better matches the active display surface.
- Docs updated:
  - `docs/sdlgpu-render-plan.md`
  - `docs/runtime-host-architecture.md`

CI baseline issue found and fixed locally:

- Commit `2fd17a5 Remove Cocos platform macros from base config` had
  `Code Format Check` success but Android build run `28402089691` failed.
- Job `84155636801` exact error:
  `cpp/core/base/SysInitIntf.cpp:49:9: error: use of undeclared identifier
  'TVPProtectInit'`.
- Cause:
  - Removing Cocos platform macros made Android compile an old protection
    branch that previously was not active in this build.
  - No `TVPProtectInit` declaration/implementation exists in the current
    Android build.
- Local fix:
  - Added `TVP_CORE_PLATFORM_ANDROID`.
  - Restricted the protection loop to
    `defined(USING_PROTECT) && !defined(_WIN32) && !TVP_CORE_PLATFORM_IOS &&
    !TVP_CORE_PLATFORM_ANDROID`.

Checks run locally:

- `git diff --check`: passed.
- `python3 -m json.tool vcpkg.json`: passed.
- Grep found no remaining Android/Flutter default `renderer=software` in the
  touched launcher/default paths.

Expected log after this patch:

- Preferred successful path:
  - `android-egl-presenter present-android-egl ... nativeGL=<nonzero>
    softwareUpload=0`
  - `sdl-gpu-presenter present-texture-egl ...`
  - `present-flutter-direct` should stop or be fallback-only.
- If still falling back:
  - `present-flutter-direct ... glBacked=1` means GL texture exists but EGL
    failed, so inspect `android-egl-presenter failure` or `auto-disabled`.
  - `present-flutter-direct ... glBacked=0` means renderer is still software or
    final texture is not GL-backed; check preference migration and per-game
    overrides.

## 2026-07-01 OpenGL EGL orientation logs

New logs inspected:

- `/root/log/20260701000026954.log`
- `/root/log/20260701000106962.log`
- `/root/log/20260701000316549.log`
- `/root/log/20260701000350893.log`
- `/root/log/20260701000427583.log`
- `/root/log/78.log`

Findings:

- OpenGL runs now enter the desired high-performance path:
  - `present-android-egl`
  - `present-texture-egl`
  - `nativeGL=5`
  - `softwareUpload=0`
- Software runs still show:
  - `reason=no-native-gl`
  - `present-flutter-direct ... glBacked=0`
  - This is expected because a software TVP texture has no native GL texture.
- The user's visual report is that OpenGL renderer flips the game image while
  software renderer does not. This isolates the problem to the EGL blit's
  texture coordinate mapping, not the Flutter Texture bridge or CPU fallback.

Reference comparison:

- `AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp` uses a host blit shader
  with optional `uFlipY` and `uUVScale`. Its vertex data maps KRKR's top-left
  image convention explicitly; comments there are not directly transferable to
  this project because our fullscreen quad initially used unflipped GL-style
  texture coordinates.
- `AetherKiri/cpp/plugins/krkrlive2d.cpp` explicitly uses Android
  `flipY=1` for a GL texture blit so the display is right-side up.
- Therefore the right fix for KiriKiri-LauncherC's current EGL presenter is to
  flip the sampled TVP GL texture by default, not to change the Flutter
  SurfaceTexture bridge or return to CPU copies.

Patch applied locally:

- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - `IsAndroidEGLSurfaceFlipYEnabled()` now defaults to true.
  - `KRKR2_ANDROID_EGL_SURFACE_FLIP_Y=0` disables the flip if a device proves
    the opposite orientation.
  - `present-android-egl` logs now include `flipY=0/1`.
- Docs updated:
  - `docs/sdlgpu-render-plan.md`
  - `docs/runtime-host-architecture.md`

Expected next OpenGL log:

- `present-android-egl ... softwareUpload=0 ... flipY=1`
- Image should no longer be vertically inverted.
2. Move Android file/path helpers away from Cocos `FileUtils` first in places
   that already use regular filesystem paths.
3. Keep Cocos UI forms and legacy `TVPWindowLayer` as the compatibility host
   until SDL3 presentation and input parity are proven.

## 2026-07-01 RuntimePresenter boundary slice

User asked to continue because rendering efficiency is still low and the
project must keep moving to the new Flutter + SDL3 architecture instead of
continuing to rely on Cocos. The hard low-level requirement was restated:
complete, high-performance, high-compatibility, and reference the stable
render/upload/present implementations in `AetherKiri`, `kirikiroid2-web`,
`KrKr2-Next`, `krkrsdl2-main`, `krkrsdl3-main`, and `SDL-release-3.4.10`
instead of inventing unusual paths.

CI baseline checked before new edits:

- `61a0a0a Fix Android EGL presenter orientation`
  - `Code Format Check`: success, run `28458837659`
  - `Build Flutter Android`: success, run `28458886177`
- Local branch was clean and matched `origin/main` at `61a0a0a`.

Subagent/reference findings from this continuation:

- `AetherKiri`:
  - Stable model is
    `SurfaceTexture -> Surface -> ANativeWindow -> EGL WindowSurface ->
    full-frame GL blit -> eglSwapBuffers`.
  - `engine_tick()` auto-attaches a pending Android native window. If EGL is
    not initialized it calls `InitializeWithWindow()`, otherwise it calls
    `AttachNativeWindow()`.
  - Its EGL manager owns `AttachNativeWindow`, `DetachNativeWindow`,
    `UpdateNativeWindowSize`, `MarkFrameDirty`, and `ConsumeFrameDirty`.
  - `HostWindowLayer::UpdateDrawBuffer()` is the real present entry: it takes
    the TVP texture, unbinds the render target, computes aspect-correct
    viewport/letterbox, updates DrawDevice destination/clip/window size, draws
    a full-frame quad with `uUVScale` and `uFlipY`, then marks the frame dirty.
  - `TVPForceSwapBuffer()` only swaps when a native window exists and
    `ConsumeFrameDirty()` returns true. This is the next important thing to
    port because it prevents redundant swaps and stale SurfaceTexture
    double-buffer flicker.
- `krkrsdl3-main` / `krkrsdl2-main` / `KrKr2-Next`:
  - SDL3 standalone uses `SDL_AppInit`, `SDL_AppEvent`, `SDL_AppIterate`; good
    reference for an SDL-owned runtime, but not safe to copy wholesale while
    Flutter owns the host surface.
  - SDL2's mature order is event pump, application idle/run, then presenter.
  - Flutter should call native through stable C ABI / host contract and should
    not depend on `TVPMainScene` or Cocos types.
  - Split `RuntimeHost` and `Presenter`: lifecycle/frame ticking/input belong
    to the host, while texture/window presentation belongs to a presenter.

Patch applied locally:

- Added `cpp/core/environ/runtime/RuntimePresenter.h`.
  - Defines `TVPRuntimeScreenTakeoverRequest`.
  - Defines `TVPRuntimeTexturePresentRequest`.
  - Defines `iTVPRuntimePresenter`.
  - Provides global helpers:
    `TVPSetRuntimePresenter`, `TVPGetRuntimePresenter`,
    `TVPGetRuntimePresenterName`,
    `TVPRuntimeSetScreenTakeoverEnabled`,
    `TVPRuntimeIsScreenTakeoverSupported`,
    `TVPRuntimeIsScreenTakeoverEnabled`,
    `TVPRuntimeHasPresentedFrame`,
    `TVPRuntimePumpScreenPresenter`,
    `TVPRuntimePresentTexture`,
    `TVPRuntimePresentHostWindowTexture`,
    `TVPRuntimeRecordOverlayFrame`.
- Added `cpp/core/environ/runtime/RuntimePresenter.cpp`.
  - Stores the active presenter in an atomic pointer.
  - Default virtual methods are safe no-ops/false.
  - Android logs presenter registration through `runtime-presenter`.
- Added `cpp/core/environ/sdl/SDLRuntimePresenter.h`.
  - Declares `TVPRegisterSDLRuntimePresenter()` and
    `TVPUnregisterSDLRuntimePresenter()`.
- Added `cpp/core/environ/sdl/SDLRuntimePresenter.cpp`.
  - Implements `TVPSDLRuntimePresenter`.
  - Adapts existing proven SDL functions:
    `TVPSDLSetScreenTakeoverEnabled`,
    `TVPSDLIsScreenTakeoverSupported`,
    `TVPSDLIsScreenTakeoverEnabled`,
    `TVPSDLHasScreenPresenterPresented`,
    `TVPSDLPumpScreenPresenter`,
    `TVPSDLTryPresentTexture`,
    `TVPSDLPresentHostWindowTexture`,
    `TVPSDLRecordRenderOverlayFrame`.
  - Presenter name is `sdl3`.
- Updated `cpp/core/environ/CMakeLists.txt`.
  - Adds `runtime/RuntimePresenter.cpp`.
  - Adds `sdl/SDLRuntimePresenter.cpp`.
- Updated `cpp/core/environ/sdl/SDLGameManager.cpp`.
  - Includes `SDLRuntimePresenter.h`.
  - `TVPSDLInitializeRuntime()` registers the SDL runtime presenter. This
    matters for future non-Cocos Flutter/SDL3 host entry points.
- Updated `cpp/core/environ/cocos2d/CocosRuntimeHost.cpp`.
  - Includes `sdl/SDLRuntimePresenter.h`.
  - `TVPRegisterCocosRuntimeHost()` now registers the SDL presenter when the
    legacy Cocos host registers. This prevents early `doStartup` presenter
    calls from becoming no-ops before SDL's explicit runtime init path runs.
- Updated `cpp/core/environ/cocos2d/MainScene.cpp`.
  - `TVPWindowLayer::UpdateDrawBuffer()` now calls
    `TVPRuntimePresentHostWindowTexture()` instead of directly calling
    `TVPSDLPresentHostWindowTexture()`.
  - Android startup takeover now calls:
    `TVPRuntimeSetScreenTakeoverEnabled`,
    `TVPRuntimeIsScreenTakeoverSupported`,
    `TVPRuntimeIsScreenTakeoverEnabled`,
    `TVPRuntimePumpScreenPresenter`,
    `TVPRuntimeHasPresentedFrame`.
  - Android overlay frame stats now call `TVPRuntimeRecordOverlayFrame()`.
  - Android update-enforced takeover pump now uses runtime presenter helpers.
  - Remaining `TVPSDL*` calls in `MainScene.cpp` are diagnostics/loading
    console/render probes, not presenter ownership.

Why this is the right next slice:

- It does not try to delete Cocos in one risky step.
- It moves the high-performance Android EGL/Flutter SurfaceTexture presenter
  behind a reusable runtime boundary.
- It matches the reference-project conclusion that Flutter/SDL3 needs a host
  contract and presenter contract, not direct `TVPMainScene` dependencies.
- It keeps the current default rendering behavior intact: the SDL presenter
  still chooses Android EGL for GL-backed textures and falls back to CPU
  Flutter `ANativeWindow_lock` when needed.

Local checks after the patch:

- `git diff --check`: passed.
- `python3 -m json.tool vcpkg.json`: passed.
- Grep confirms no direct Cocos `MainScene.cpp` calls remain to
  `TVPSDLSetScreenTakeoverEnabled`,
  `TVPSDLIsScreenTakeoverSupported`,
  `TVPSDLHasScreenPresenterPresented`,
  `TVPSDLPumpScreenPresenter`, or `TVPSDLPresentHostWindowTexture`.

Known remaining limitations:

- `SDLGameManager.cpp` is still too large and still owns Android EGL
  presenter state directly.
- The current EGL presenter swaps immediately inside
  `TryPresentAndroidEGLSurfaceTexture()`. It does not yet have AetherKiri's
  explicit `MarkFrameDirty` / `ConsumeFrameDirty` swap gate.
- Aspect-correct viewport and input-transform synchronization are still not
  as complete as AetherKiri's `HostWindowLayer::UpdateDrawBuffer()` model.
- Software textures still use CPU fallback by default; the EGL software upload
  path remains diagnostic-only. This is compatible but not final performance.

Next concrete rendering slice:

1. Extract Android EGL SurfaceTexture state out of `SDLGameManager.cpp` into a
   dedicated SDL runtime presenter module.
2. Port AetherKiri-style `AttachNativeWindow`, `DetachNativeWindow`,
   `UpdateNativeWindowSize`, `MarkFrameDirty`, and `ConsumeFrameDirty`.
3. Move swap control toward a dirty-gated presenter lifecycle.
4. Add viewport/dest-rect synchronization to the runtime presenter so display
   and input mapping can stay correct when Flutter surface aspect ratio differs
   from the game texture.

## 2026-07-01 OpenGL color and render-module management slice

User reported that the previous OpenGL render pipeline plus Vulkan/backend path
is now very satisfactory: frame pacing is stable and performance is high.
Software rendering is still weak, around 10 FPS. Remaining visual issue:
OpenGL renders the colorful main-menu background as black-and-white while other
content appears correct. User also asked for deeper bottom-layer logic and
module-level management: each renderer should have its own manager, and the
runtime should call the appropriate manager instead of continuing to pile logic
into Cocos or one global SDL file.

Logs inspected:

- `/root/log/20260701014233815.log`
- `/root/log/20260701014327699.log`
- `/root/log/20260701014407678.log`
- `/root/log/78.log`

Findings:

- `20260701014233815.log` and `20260701014327699.log` are software/fallback
  runs:
  - `present-android-egl`: 0
  - `present-texture-egl`: 0
  - `present-flutter-direct`: 15 / 14
  - `reason=no-native-gl`: 15 / 14
  - This matches the user's observation that software rendering remains slow:
    it still goes through CPU texture access and Flutter `ANativeWindow_lock`.
- `20260701014407678.log` is the desired OpenGL/EGL path:
  - `present-android-egl`: 19
  - `present-texture-egl`: 19
  - `present-flutter-direct`: 0
  - `softwareUpload=0`: 19
  - `flipY=1`: 19
  - `android-egl-presenter failure`: 0
  - Therefore the black-and-white background is not caused by the
    SurfaceTexture presenter or EGL swap path. It is almost certainly upstream
    in OpenGL texture upload/format handling.

Root cause candidate found and patched:

- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  `tTVPOGLTexture2D_split::AsSingleTexture()` downscales large static textures.
- Before this patch it always allocated a 4-channel temp buffer and used
  `CV_8UC4`, but then uploaded the buffer using the texture's original format:
  `GL_LUMINANCE` for Gray, `GL_RGB` for RGB, or `GL_RGBA` for RGBA.
- For RGB static large images this meant a 4-byte RGBA/BGRA row could be
  interpreted as 3-byte RGB during upload. For Gray it could be interpreted as
  1-byte luminance. That explains colorful large menu/background images turning
  grayscale/incorrect on OpenGL while other content stays correct.
- Patch:
  - Added `TVPSetGLUnpackAlignmentForPitch()`.
  - `AsSingleTexture()` now selects `CV_8UC1`, `CV_8UC3`, or `CV_8UC4` based on
    `TVPTextureFormat::Gray`, `RGB`, or `RGBA`.
  - The temporary upload buffer size is now `internalW * internalH * pixsize`,
    not always `* 4`.
  - `glTexImage2D()` receives data whose byte layout matches `pixfmt`.

Texture cache/budget patch:

- Ported the safer part of AetherKiri's split-texture cache design:
  - `MAX_SPLIT_CACHE_ENTRIES = 8`
  - `_cachedVMemBytes`
  - `ClearTextureCache()` subtracts cached bytes from `_totalVMemSize`
  - cache is cleared before adding a ninth split cache entry
- This gives large static images a bounded GL cache instead of letting split
  texture entries grow indefinitely.
- Destructor now handles the `AsSingleTexture()` case where `Bitmap` was
  released early to reduce memory peak.

New bottom-layer render management:

- Added `cpp/core/environ/runtime/RuntimeRenderManager.h`.
  - Defines `TVPRuntimeRenderModuleInfo`.
  - Defines `TVPRuntimeRenderManagerSnapshot`.
  - Exposes `TVPRuntimeUpdateRenderManagerSnapshot()`,
    `TVPRuntimeGetRenderManagerSnapshot()`, and
    `TVPRuntimeDescribeRenderManager()`.
- Added `cpp/core/environ/runtime/RuntimeRenderManager.cpp`.
  - Stores the active render snapshot behind a mutex.
  - Formats a stable diagnostic string that includes:
    `pipeline`, `presenter`, `backend`, `presenterFast`, `cpuCopyFree`, and
    per-module summaries.
- Updated `cpp/core/environ/CMakeLists.txt`.
  - Adds `runtime/RuntimeRenderManager.cpp`.
- Updated `cpp/core/environ/sdl/SDLGameManager.cpp`.
  - `TVPSDLRecordRenderOverlayFrame()` now builds a
    `TVPRuntimeRenderManagerSnapshot`.
  - It records active TVP pipeline, active presenter, selected graphics
    backend, draw count, video memory, presented frame count, high-performance
    presenter flag, and CPU-copy-free presenter flag.
  - Overlay text now starts from `TVPRuntimeDescribeRenderManager()`, then
    appends existing SDL_GPU and Android EGL counters.

Architecture rule after this slice:

- `RuntimeHost` owns lifecycle and ticking.
- `RuntimePresenter` owns frame delivery to host surfaces.
- `RuntimeRenderManager` owns module-level renderer/presenter/backend state and
  budget/capability reporting.
- The next major migration should create concrete software/OpenGL/EGL/SDL_GPU
  manager modules and move logic out of `SDLGameManager.cpp` behind these
  boundaries.
