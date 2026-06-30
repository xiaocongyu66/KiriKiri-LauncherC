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

The initial implementation registers a `cocos2d` host from
`CocosRuntimeHost.cpp` and routes `TVPSDLRunGameLaunch()` startup through the
active host. `TVPMainScene::update()` also delegates the per-frame engine tick
through `iTVPRuntimeHost::RunFrame()`. File-selector launches and Flutter
overlay launch requests also call `TVPStartGameOnRuntimeHost*()` now. The
helper centralizes host lookup, launch-result diagnostics, and error mapping
for C ABI callers. Current behavior remains unchanged, but launch
orchestration and frame ticking no longer hard-code the Cocos scene as the
owner.

## Presenter Boundary

`cpp/core/environ/runtime/RuntimePresenter.h` is the host-independent
presentation boundary. It exists so the Flutter + SDL3 path can take over
screen ownership without making new code depend on `TVPMainScene` or Cocos
types.

A presenter owns:

- screen takeover enablement and capability checks
- texture presentation from TVP draw devices
- host-window size synchronization after a successful present
- Flutter/SDL overlay frame statistics
- fallback pumping for legacy SDL window/surface presenters

`cpp/core/environ/sdl/SDLRuntimePresenter.cpp` is the first presenter
implementation. It adapts the existing SDL3/Android presenter functions,
including the Android EGL/SurfaceTexture path and the CPU
`ANativeWindow_lock` fallback. The Cocos host registers this SDL presenter when
it registers the legacy runtime host, and `TVPSDLInitializeRuntime()` registers
the same presenter when SDL is initialized independently. This keeps current
Android behavior intact while making the presenter reusable by a future
Flutter/SDL3 runtime host.

`cpp/core/environ/sdl/SDLPresentTypes.h` is the first shared presenter pipeline
type boundary. It names the active present path and carries the planned dirty
rectangle, fallback rectangle, and actual present result. New SDL presenter
modules should return these result types instead of leaking Android/EGL local
state back into `SDLGameManager.cpp`.

The important architectural rule is that Cocos may call the runtime presenter
while it remains the compatibility host, but the presenter must not require a
Cocos scene. Future SDL3 host work should call the same presenter contract
directly.

## Render Manager Boundary

`cpp/core/environ/runtime/RuntimeRenderManager.h` is the low-level render
module registry and status boundary. It does not render frames by itself.
Instead, each renderer/presenter/backend module reports a small snapshot:

- TVP render pipeline name, such as `Software` or `OpenGL`
- active presenter name, such as the SDL renderer, SDL surface fallback, or
  Android EGL SurfaceTexture presenter
- selected graphics backend, such as `opengl`, `vulkan`, or `gpuapi`
- per-frame draw count and video-memory usage
- module capability flags, including high-performance and CPU-copy-free paths
- future texture-budget fields for renderer-specific cache policy

This is the bottom-layer management direction for the migration: every renderer
gets its own implementation and budget policy, while the runtime render manager
keeps the current active state queryable by Flutter, SDL3, diagnostics, and
future scheduling logic. It replaces ad hoc renderer string construction in
host code and gives the next SDL3 host a stable place to decide which module to
drive.

`cpp/core/visual/ogl/krkr_texture2d.h` is the current texture-adapter boundary
for removing Cocos from the renderer surface. `RenderManager.h`,
`RenderManager.cpp`, and `RenderManager_ogl.cpp` now talk about
`krkr::Texture2D` rather than exposing `cocos2d::Texture2D` directly. During the
legacy host phase, `krkr::Texture2D` is a compatibility alias to the Cocos
texture type so `TVPMainScene` can still pass adapter textures to Cocos sprites.
After the Flutter + SDL3 host owns presentation, this header should switch to
the standalone AetherKiri-style texture wrapper and the Cocos alias can be
deleted.

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
2. Move launch and frame metrics from `TVPMainScene` methods into
   `runtime/RuntimeHost` methods.
3. Move presentation calls into `runtime/RuntimePresenter` so host lifecycle
   and frame delivery can evolve independently.
4. Route renderer/presenter/backend status through
   `runtime/RuntimeRenderManager` instead of per-host ad hoc checks.
5. Move loading console and input handoff behind runtime/platform services.
6. Add an SDL3 runtime host that owns the frame loop and presenter without
   requiring a Cocos scene.
7. Keep Flutter as launcher/UI shell and call native C ABI/runtime host methods
   for launch, settings, diagnostics, and in-game menu actions.
8. Once Android and desktop smoke tests pass with the SDL3 host, turn
   `KRKR2_ENABLE_COCOS_HOST` off in a CI audit job, then delete the legacy host
   after parity.

## Performance Rule

Fast-forward mode must not be limited by a UI framework render loop. The SDL3
host should present dirty game frames directly through the native presenter and
only notify Flutter/Cocos for UI overlay state. Flutter should not receive every
game-frame copy when no launcher or menu UI needs it. The current Android
hybrid path presents through the Flutter external texture directly and does not
enable the SDL surface mirror on Android by default, because that mirror copies
regions from TVP textures and can force GPU-backed textures through CPU pixel
readback. Set `KRKR2_ENABLE_ANDROID_SDL_SURFACE_MIRROR=1` only for diagnostics
or legacy fallback investigation. Fast-forward performance should move to an
SDL3 GPU/SurfaceTexture native presenter instead of throttling those copies.
The Android EGL/SurfaceTexture path is now the default presenter for GL-backed
TVP textures. It draws the native GL texture into Flutter's SurfaceTexture with
`eglSwapBuffers` and only falls back to `ANativeWindow_lock` CPU copies when EGL
is unavailable, explicitly disabled, or repeatedly fails before the first EGL
present. The GL texture blit flips Y by default because the native TVP texture
comes from the OpenGL/FBO path while the game image convention is top-left
oriented; software CPU fallback remains unflipped. The CPU fallback now locks
and posts full frames by default because partial `ANativeWindow` dirty updates
can reuse stale contents from another Android buffer during fast scene changes.
`KRKR2_ANDROID_DIRECT_PARTIAL_PRESENT=1` keeps the old partial path available
for diagnostics only.

AetherKiri's strongest reference point is its native-window presenter
lifecycle: attach the Flutter/Android `ANativeWindow` as an EGL WindowSurface,
draw the final TVP texture into that surface, mark the frame dirty, and only
swap when the frame is dirty. KiriKiri-LauncherC now has the runtime presenter
contract needed to move toward that design without growing the Cocos scene
again. The next rendering slice should move the Android EGL surface state out
of `SDLGameManager.cpp` and add the same dirty/swap gate before the SDL3 host
becomes the default owner.
