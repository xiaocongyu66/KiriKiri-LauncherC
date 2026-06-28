# SDL Migration Plan

This document records the current SDL migration direction for the KRKR
launcher/runtime. Review it before each SDL migration change so the work stays
incremental and does not accidentally replace launcher behavior.

## Source Of Truth

- `/root/krkrsdl2` is a KRKR-specific SDL2 platform adaptation, not a plain SDL
  package.
- `/root/krkrsdl2/external/SDL` is SDL 2.0.22. Do not copy or vendor it into
  this project.
- `/root/krkr2` should keep the current modern SDL2 dependency from vcpkg
  (`sdl2` 2.32.10#1) and port the KRKR adaptation concepts/code in small steps.

## Current krkrsdl2 Architecture

- Android starts from `KirikiriSDL2Activity extends SDLActivity`.
- Native startup uses `SDL_main`, then initializes KRKR platform state and runs
  the KRKR main loop.
- The main loop processes SDL events, runs `ApplicationIdle()`, then ticks KRKR
  windows.
- Rendering presents completed KRKR bitmap regions through SDL surface/texture
  objects and `SDL_RenderPresent`, not through Cocos.
- Native event queues use SDL custom events (`SDL_RegisterEvents` and
  `SDL_PushEvent`) instead of Cocos message dispatch.
- Audio selection prefers FAudio when available and otherwise falls back to a
  null device, so it should not be copied directly over the existing mixed
  OpenAL/Oboe/SDL audio code.

## Current krkr2 State

- Game launch has started moving through `SDLGameManager`.
- Cocos still owns Android Activity startup, scene/window lifecycle, UI,
  in-game rendering, and much of input routing.
- Before Step 1, `NativeEventQueue` posted through
  `Application->PostUserMessage`, which kept it tied to the Cocos-driven frame
  loop.
- The launcher UI, `gameDir`, `custom_launch`, `EXTRA_LAUNCH_FILE`, and the
  left yellow/gray loading log must be preserved during migration.

## Migration Order

1. Move KRKR internal native events to an SDL custom event queue while preserving
   the existing `Application->ProcessMessages()` fallback.
2. Add SDL event-pump diagnostics and keep all logs on the existing unified
   native log path.
3. Port the bitmap-completion presenter model from `krkrsdl2` in two phases:
   first mirror/probe KRKR frame textures in SDL-managed code without presenting
   them, then copy safe CPU-backed frames into SDL surfaces/textures.
4. Add SDL input mapping modeled on `krkrsdl2`, routing into existing KRKR input
   events instead of bypassing script expectations.
5. Move game window presentation to the SDL presenter while keeping launcher UI
   and loading console intact.
6. Move audio backend ownership/logging under the SDL runtime manager only after
   render/input lifecycles are stable.
7. Remove Cocos runtime dependencies only after launcher UI and game rendering
   have separate, working SDL-owned paths.

## Current Implementation Status

- Step 1 has started.
- `NativeEventQueue` now has an SDL custom event backend with `Application`
  message fallback if SDL event initialization or push fails.
- `Application::ProcessMessages()` pumps the SDL native event queue once per
  frame before processing legacy user messages and timers.
- Native event queue diagnostics use the unified native log tag
  `sdl-eventqueue`.
- SDL runtime initialization explicitly calls `SDL_SetMainReady()` in the
  hybrid Cocos startup path to avoid false SDL_main initialization errors.
- Android Java SDL version/library diagnostics are logged during launch so Java
  SDL and native SDL versions can be compared from one log.
- Launcher log writes use the native log bridge after native file logging is
  configured, preventing Java file appends and native spdlog writes from
  interleaving in the unified log.
- Unified Android log sessions now use millisecond-resolution file names. This
  prevents two quick restarts in the same minute from reusing the same log file
  and merging the previous native tail with the next launcher session header.
- Android input events are now mirrored into an SDL custom input queue tagged
  `sdl-inputqueue`. The old Cocos input path still receives the events, but SDL
  owns a parallel queue that is drained once per `Application::ProcessMessages()`
  cycle for diagnostics and future KRKR input routing.
- Logs from `20260606194656952.log` confirm the SDL event queue and SDL input
  queue initialize correctly on Android, drain normally, and report `dropped=0`.
- SDL input queue diagnostics now include event details, backlog, maximum drain
  age, and high-water backlog counters so input stalls can be tied to a concrete
  event batch.
- Android direct-touch queue processing now coalesces contiguous high-frequency
  `touch-move` events for the same pointer and tracks them separately from
  dropped events. Once the first frame has presented, stale-event dropping is
  limited to move events so begin/end and cancel ordering is preserved during
  render or script stalls.
- The Cocos draw-buffer update path now reports `sdl-renderprobe` entries with
  frame size, internal texture size, texture-change count, SDL subsystem state,
  and texture pointers. This is a non-presenting probe for the future SDL
  bitmap presenter.
- `BasicDrawDevice::Show()` now reports each visible KRKR frame to an SDL
  presenter mirror tagged `sdl-presenter`. It records texture size, internal
  size, format, pitch, native GL texture id, SDL subsystem state, and whether a
  CPU scanline can be safely sampled. GL-backed textures are identified through
  `GetNativeGLTextureId()` and are not read back in this probe, avoiding
  per-frame `glReadPixels()` stalls while Cocos still owns presentation.
- Bitmap completion is now mirrored into SDL diagnostics tagged `sdl-bitmap`.
  `BasicDrawDevice` records completion batch start/end, and
  `LayerManager::DrawCompleted()` records the real completed regions before the
  existing draw-buffer `Blt`. This exposes the future SDL surface update stream
  without changing Cocos presentation.
- Safe CPU-backed bitmap completion regions are now copied into an SDL-owned
  `SDL_Surface` mirror tagged `sdl-surface`. The copy path follows the
  `/root/krkrsdl2` model: validate the destination and source clip, allocate a
  surface matching the KRKR primary layer size, copy dirty RGBA rows, and keep a
  union update rectangle for the future SDL texture presenter. Unsupported or
  failed copies are logged through the unified native log with throttling.
- The old left yellow/gray startup console remains visible through the existing
  Cocos `TVPConsoleWindow`, and its state is now mirrored into SDL runtime state
  tagged `sdl-loading`. `show`, `line`, and `hide` events use the unified native
  log so the SDL presenter can draw the same loading console once it owns game
  presentation.
- Android/iOS now use the legacy KRKR mobile virtual screen width of 2048 for
  the Cocos design resolution, with height derived from the physical display
  aspect ratio. Logs still include both physical frame size and virtual scene
  size so the SDL presenter can keep the same coordinate base during handoff.
- SDLUI replacement has started. `SDLUIManager` now registers the complete
  legacy `/root/krkr2/ui/cocos-studio` asset family as SDLUI templates: all 35
  Cocos Studio `.csd/.csb` forms under `out_ui/` and `ui/`, plus the native
  `loading-console` target. Images continue to come from `img/`, locale XML
  from `locale/`, and text rendering keeps `NotoSansCJK-Regular.ttc`. The
  existing Cocos nodes still render most forms for this increment, but SDLUI is
  now the runtime resource and state boundary that future SDL drawing and input
  handling will consume.
- The in-game `GameMainMenu` is now the first UI extraction target. Its Cocos
  Studio widgets are treated as layout/resource input only: SDLUI records the
  five legacy buttons as an action model (`game-menu`, `window-manager`,
  `mouse-mode`, `keyboard`, `exit`), tracks their scene rectangles, and logs a
  `game-menu render-intent` that the future SDL renderer can draw without
  depending on Cocos widget names. Cocos remains a temporary display shell for
  this component until the SDL renderer path is visible.
- Message boxes and simple progress boxes now mirror their SDLUI state while
  the Cocos form still renders them. SDLUI records show/close sessions,
  captions, body text lengths, button labels/actions, progress title/content,
  progress text, percentage updates, and secondary progress visibility through
  the unified `sdl-ui` log, giving the future SDLUI renderer the same state
  boundary without depending on Cocos callbacks.
- UI replacement should continue by extracting each `/root/krkr2/ui` Cocos
  Studio form into an SDLUI model that reuses the old images, fonts, locale
  strings, and CSD layout data rather than introducing Qt/wxWidgets on Android
  or copying the Cocos framework. Recommended order: loading console,
  `GameMainMenu`, message box, window manager, file selector, settings forms.
- Android audio renderer selection now tries the SDL2 audio device first and
  falls back to Oboe, then OpenAL if SDL cannot open a playback device. Backend
  selection, device parameters, conversion failures, stream creation, and
  release events are logged with the unified `sdl-audio` tag.
- Android game startup requests an experimental SDL screen takeover path after
  the KRKR window list is visible. In the current Cocos2dxActivity hybrid path,
  Android SDL window creation is deferred by default because `krkrsdl2` creates
  its window from SDLActivity/SDL_main ownership, not from an already-running
  Cocos surface. The SDL surface mirror, dirty rectangle stream, input queue,
  audio backend, and `sdl-screen` diagnostics still run, but Cocos `UINode`,
  `GameNode`, and the floating game menu are only hidden after the SDL presenter
  has successfully presented at least one frame. This prevents the diagnostic
  handoff from crashing or black-screening while the full SDLActivity/runtime
  replacement is still incomplete. A forced window-creation test can be enabled
  only by defining `KRKR2_ENABLE_HYBRID_SDL_SCREEN_WINDOW`.
- A runtime-host boundary now exists in `cpp/core/environ/runtime`. Cocos
  registers itself as the current host through `CocosRuntimeHost.cpp`, and game
  launch orchestration calls the active host instead of directly calling
  `TVPMainScene::startupFrom()`. `TVPMainScene::update()` also delegates the
  per-frame engine tick through `iTVPRuntimeHost::RunFrame()`, with the Cocos
  host preserving the old `Application->Run()` behavior. This is the first step
  toward making SDL3 the owner of lifecycle and presentation.
- File-selector launches, Flutter overlay launch requests, and the app-delegate
  startup callback now use the shared `TVPStartGameOnRuntimeHost*()` helper so
  host lookup, result logging, and C ABI error mapping are centralized for the
  future SDL3 host.
- `KRKR2_ENABLE_COCOS_HOST` now guards the legacy Cocos host source and target
  links. It defaults to `ON`; `OFF` is only a dependency audit mode until the
  SDL3 host and Flutter/C ABI launcher path reach parity.
- Android Flutter direct-surface dirty-rectangle cadence throttling was
  removed. Dropping intermediate dirty regions before `ANativeWindow` posting
  can expose stale or partial buffers during fast-forward sprite updates. The
  current hybrid path must preserve every valid dirty copy for correctness; the
  performance replacement target is an SDL3 GPU/SurfaceTexture native presenter
  that keeps game frames out of Flutter's CPU copy path.
- Logs from `20260606202512770.log` show the first-run black interval is
  startup-bound rather than an SDL input failure: Android resumes quickly, SDL
  2.32.10 initializes, input queues drain with `dropped=0`, but the first KRKR
  window/draw buffer appears several seconds after `StartApplication()` begins.
  The loading console is now created in `startupFrom()` and `doStartup()` is
  delayed briefly so the existing yellow/gray console can present before the
  synchronous KRKR startup work blocks the first game frame.
- Cocos remains in charge of UI, scene startup, and current game presentation.

## Guardrails

- Do not delete Cocos in one change.
- Do not import `/root/krkrsdl2/external/SDL`.
- Keep migration changes scoped and reversible.
- Use existing unified native logging (`TVPNativeLogInfo`) for diagnostics.
- Avoid game-specific special cases.
- Local Android SDK may be missing, so CI/GitHub builds remain authoritative.
