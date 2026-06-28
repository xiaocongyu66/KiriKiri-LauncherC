# SDL_GPU render migration

## Current baseline

- Stable fallback renderers remain `software` and `opengl`.
- Render selection is two-dimensional: `renderer` selects the TVP render
  pipeline (`software` or `opengl`), while `graphics_backend` selects the
  presenter/device backend (`opengl`, `vulkan`, or `gpuapi`). Valid
  combinations include `opengl+vulkan`, `opengl+opengl`,
  `software+vulkan`, and `software+gpuapi`.
- `sdlgpu` is compiled as an independent core module under `cpp/core/render/sdlgpu`.
- The first backend layer provides SDL_GPU device creation, texture creation, texture upload, release, and basic memory/upload statistics.
- `SDLGpuTvpAdapter` now owns TVP texture format mapping, region upload validation, and RGB-to-RGBA staging for future `CreateTexture2D` / `UpdateTexture2D` routing.
- `SDLGpuTextureCache` owns SDL_GPU texture handles, 10 MiB single-texture and 256 MiB total budgets, and LRU eviction for the future TVP upload path.
- Selecting `graphics_backend=gpuapi` enables the SDL_GPU texture cache/shadow
  upload path while presentation still uses the active host presenter. This is
  intentionally separate from the `renderer` pipeline selection.
- Android builds request SDL3's Vulkan feature so SDL_GPU has a device backend
  when the platform supports it. Runtime device selection is compatibility
  first: SDL_GPU uses SDL's automatic driver selection unless
  `KRKR2_SDL_GPU_DRIVER` explicitly names a driver. This avoids treating
  `SDL_GetGPUDriver()` enumeration as proof that a specific backend can pass
  `PrepareDriver()` on the current Android device.
- Android SDL_GPU device creation uses `SDL_CreateGPUDeviceWithProperties` and
  relaxes SDL's Vulkan optional feature requirements by default: clip distance,
  depth clamping, indirect draw first instance, and anisotropy. Set
  `KRKR2_SDL_GPU_STRICT_FEATURES=1` to restore strict Android feature
  requirements for diagnostics.
- If SDL still cannot create an SDL_GPU device at runtime, the shadow upload
  path logs the preferred driver, available driver list, and SDL error once,
  including the selected shader formats and strict/relaxed feature profile,
  then stays disabled; Flutter direct presentation continues to carry the
  frame.
- With `showfps` enabled, the Flutter overlay reports the TVP pipeline,
  presenter, selected `graphics_backend`, and SDL_GPU shadow-upload state
  (`sdlgpu=<driver>` or `sdlgpu=unavailable ... reason=...`).
- Android hybrid builds keep Cocos as the active presenter until an SDL or
  Flutter texture presenter has successfully presented a frame; takeover may be
  requested earlier, but Cocos is only hidden after the presenter is ready.
- The SDL surface mirror copies frames only while a real screen presenter consumer is active, so Android hybrid builds do not pay the extra CPU copy cost.

## Migration order

1. Keep `software` as the compatibility fallback.
2. Route texture allocation/upload through a TVP adapter that can choose `software`, `opengl`, or `sdlgpu`.
3. Implement SDL_GPU cache eviction with the existing texture budget policy.
4. Move Layer compositing operations to SDL_GPU render passes incrementally.
5. Replace the cocos present path after SDL_GPU texture and compositing paths are stable.
6. Promote `gpuapi` from shadow-upload diagnostics to a full presenter only
   after startup, touch, video, and fast-skip tests pass.

## Initial backend scope

- Supported upload formats: `R8`, `RGBA8`, `BGRA8`.
- Supported texture usage: sampler and optional color target.
- Out-of-bounds and oversized upload requests fail before touching SDL.
- The backend does not own windows or swapchains yet.
