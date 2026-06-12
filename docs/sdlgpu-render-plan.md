# SDL_GPU render migration

## Current baseline

- Stable fallback renderers remain `software` and `opengl`.
- `sdlgpu` is compiled as an independent core module under `cpp/core/render/sdlgpu`.
- The first backend layer provides SDL_GPU device creation, texture creation, texture upload, release, and basic memory/upload statistics.
- No game path is switched to SDL_GPU yet.
- Android hybrid builds keep Cocos as the active presenter until an SDL or Flutter texture presenter is actually available; SDL screen takeover requests stay disabled in this mode.
- The SDL surface mirror copies frames only while a real screen presenter consumer is active, so Android hybrid builds do not pay the extra CPU copy cost.

## Migration order

1. Keep `software` as the compatibility fallback.
2. Route texture allocation/upload through a TVP adapter that can choose `software`, `opengl`, or `sdlgpu`.
3. Implement SDL_GPU cache eviction with the existing texture budget policy.
4. Move Layer compositing operations to SDL_GPU render passes incrementally.
5. Replace the cocos present path after SDL_GPU texture and compositing paths are stable.
6. Expose `sdlgpu` in launcher settings only after startup, touch, video, and fast-skip tests pass.

## Initial backend scope

- Supported upload formats: `R8`, `RGBA8`, `BGRA8`.
- Supported texture usage: sampler and optional color target.
- Out-of-bounds and oversized upload requests fail before touching SDL.
- The backend does not own windows or swapchains yet.
