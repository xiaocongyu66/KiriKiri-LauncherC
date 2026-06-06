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
3. Port the bitmap-completion presenter model from `krkrsdl2` so game frames can
   be copied into SDL-managed textures without removing the Cocos UI yet.
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
- Cocos remains in charge of UI, scene startup, and current game presentation.

## Guardrails

- Do not delete Cocos in one change.
- Do not import `/root/krkrsdl2/external/SDL`.
- Keep migration changes scoped and reversible.
- Use existing unified native logging (`TVPNativeLogInfo`) for diagnostics.
- Avoid game-specific special cases.
- Local Android SDK may be missing, so CI/GitHub builds remain authoritative.
