# Runtime Host Architecture

The migration target is to make Cocos only one runtime host implementation, not
the owner of game lifecycle. Core engine code should depend on a small host
contract and should not know whether the active host is Cocos, SDL3, or a
Flutter-backed platform shell.

## References

This direction follows the existing project docs and the local reference
projects:

- `docs/sdl-migration-plan.md`: migrate incrementally and keep launcher
  behavior intact.
- `docs/platform-migration-boundaries.md`: Flutter owns launcher UI, C ABI owns
  engine actions, platform code owns OS lifecycle/surfaces.
- `/root/kiriki-work/krkrsdl3-main`: SDL3 owns `SDL_AppInit`,
  `SDL_AppEvent`, and `SDL_AppIterate`; KRKR frames are presented through
  SDL/GL textures without Cocos.
- `/root/kiriki-work/KrKr2-Next` and `/root/kiriki-work/AetherKiri`: product
  shells and bridge libraries are split from `cpp/core`.

## Host Boundary

`cpp/core/environ/runtime/RuntimeHost.h` is the native boundary for runtime
ownership. A host owns:

- game launch requests and preference root selection
- frame ticking
- physical frame size, virtual scene size, and scaling data
- loading console handoff
- input and text/IME handoff
- game-frame presentation handoff

The initial implementation registers a `cocos2d` host from
`CocosRuntimeHost.cpp` and routes `TVPSDLRunGameLaunch()` startup through the
active host. `TVPMainScene::update()` also delegates the per-frame engine tick
through `iTVPRuntimeHost::RunFrame()`. File-selector launches and Flutter
overlay launch requests also call `TVPStartGameOnRuntimeHost*()` now. The
helper centralizes host lookup, launch-result diagnostics, and error mapping
for C ABI callers. Current behavior remains unchanged, but launch
orchestration and frame ticking no longer hard-code the Cocos scene as the
owner.

## Build Switch

`KRKR2_ENABLE_COCOS_HOST` defaults to `ON`. It keeps current CI behavior and
legacy desktop/Android builds intact.

Use `OFF` only as a dependency audit switch for now:

```bash
cmake -S /root/kiriki-work/KiriKiri-LauncherC \
  -B /tmp/krkr2-no-cocos-audit \
  -DKRKR2_ENABLE_COCOS_HOST=OFF
```

The `OFF` mode is expected to expose remaining Cocos dependencies until these
areas are moved behind host/platform services:

- Android input and JNI helpers currently using Cocos event APIs
- old Cocos Studio UI forms under `cpp/core/environ/ui`
- file/path utilities currently using `cocos2d::FileUtils`
- crash upload helpers currently using Cocos network/base64 helpers
- platform entry points in `platforms/*/main.cpp` and Android native init

## Migration Order

1. Keep `KRKR2_ENABLE_COCOS_HOST=ON` as the shipping path.
2. Move launch, frame metrics, loading console, input, and presentation calls
   from `TVPMainScene` methods into `runtime/RuntimeHost` methods.
3. Add an SDL3 runtime host that owns the frame loop and presenter without
   requiring a Cocos scene.
4. Keep Flutter as launcher/UI shell and call native C ABI/runtime host methods
   for launch, settings, diagnostics, and in-game menu actions.
5. Once Android and desktop smoke tests pass with the SDL3 host, turn
   `KRKR2_ENABLE_COCOS_HOST` off in a CI audit job, then delete the legacy host
   after parity.

## Performance Rule

Fast-forward mode must not be limited by a UI framework render loop. The SDL3
host should present dirty game frames directly through the native presenter and
only notify Flutter/Cocos for UI overlay state. Flutter should not receive every
game-frame copy when no launcher or menu UI needs it. The current Android
hybrid `ANativeWindow` dirty-copy path is kept correctness-first and must not
drop partial dirty regions; fast-forward performance should move to an SDL3
GPU/SurfaceTexture native presenter instead of throttling those copies.
