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
  - investigate AetherKiri-style EGL attachment to Flutter `SurfaceTexture`
    as the future zero-copy path.

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

1. Finish and push the follow-up patch from the subagent reviews:
   - safe owned-batch input coalescing;
   - stale move direct-touch cancellation;
   - RuntimeHost empty-path diagnostics;
   - SDL_GPU verbose/hint fixes;
   - skip Android direct-present shadow uploads unless explicitly requested;
   - update docs and memory.
2. Run available local checks:
   - `git diff --check`;
   - `python3 -m json.tool vcpkg.json`;
   - grep changed symbols for obvious declaration/order mistakes.
3. Commit with a message like
   `Improve SDL GPU Android compatibility`.
4. Push to `origin/main` using one-shot credential injection if needed.
5. Monitor GitHub Actions for the new commit. Fix CI failures immediately.
6. Next patch after CI:
   - validate input queue coalescing on device logs and tune thresholds;
   - or start an AetherKiri-style EGL/SurfaceTexture experimental presenter
     behind an env flag, without making it default until verified.
