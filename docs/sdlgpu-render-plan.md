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
- Selecting `graphics_backend=gpuapi` requests SDL_GPU device initialization and
  reports the selected driver in diagnostics while presentation still uses the
  active host presenter. On Android, if the active presenter is the Flutter
  direct `ANativeWindow` path, per-frame SDL_GPU shadow uploads are skipped by
  default because no SDL_GPU swapchain consumes them yet and the direct
  presenter still performs the required CPU copy. Set
  `KRKR2_ENABLE_SDL_GPU_SHADOW_UPLOAD=1` to force the old shadow-upload
  diagnostics.
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
  path logs the preferred driver, compiled candidate driver list, and SDL error
  once, including the selected shader formats and strict/relaxed feature
  profile, then stays disabled; Flutter direct presentation continues to carry
  the frame.
- If SDL_GPU initializes while Android Flutter direct presentation is active,
  logs report `shadow-upload skipped ... reason=android-direct-flutter-presenter`
  unless explicit shadow upload diagnostics are enabled. This keeps `gpuapi`
  from doubling per-frame texture readback/upload cost before a real
  SDL_GPU/SurfaceTexture presenter exists.
- Android has an opt-in EGL/SurfaceTexture presenter experiment behind
  `KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT=1`. It reuses the existing Flutter
  `TextureRegistry.SurfaceTextureEntry` bridge, acquires the same
  `ANativeWindow`, creates an EGL `WindowSurface` on the render thread, draws
  the final native GL texture as a full-frame quad, and presents with
  `eglSwapBuffers`. Any failure falls back to the stable direct
  `ANativeWindow_lock` CPU presenter. Software textures are intentionally not
  uploaded by this path unless
  `KRKR2_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE=1` is set for diagnostics.
- The EGL/SurfaceTexture path never partial-swaps dirty rectangles. Dirty state
  only decides whether a frame should be presented; when the path presents, it
  redraws the full source texture to avoid stale back-buffer contents. Set
  `KRKR2_ANDROID_EGL_SURFACE_FLIP_Y=1` only if device logs/screenshots prove
  the external Flutter texture is vertically inverted.
- With `showfps` enabled, the Flutter overlay reports the TVP pipeline,
  presenter, selected `graphics_backend`, SDL_GPU shadow-upload state
  (`sdlgpu=<driver>` or `sdlgpu=unavailable ... reason=...`), and opt-in
  Android EGL presenter counters (`androidEgl=...`) when the experiment is
  enabled.
- Android hybrid builds keep Cocos as the active presenter until an SDL or
  Flutter texture presenter has successfully presented a frame; takeover may be
  requested earlier, but Cocos is only hidden after the presenter is ready.
- The SDL surface mirror is not enabled by default on Android hybrid builds.
  It can force GPU-backed TVP textures through CPU scanline readback while
  bitmap regions complete, so Android now uses the Flutter external texture
  direct presenter as the default path. Set
  `KRKR2_ENABLE_ANDROID_SDL_SURFACE_MIRROR=1` only when diagnosing the legacy
  SDL surface presenter fallback.

## Migration order

1. Keep `software` as the compatibility fallback.
2. Route texture allocation/upload through a TVP adapter that can choose `software`, `opengl`, or `sdlgpu`.
3. Implement SDL_GPU cache eviction with the existing texture budget policy.
4. Move Layer compositing operations to SDL_GPU render passes incrementally.
5. Validate the opt-in Android EGL/SurfaceTexture presenter on real devices:
   nonblank first frame, continuous frames, correct orientation, responsive
   touch, resize/detach safety, and no fallback spam.
6. Replace the cocos present path after SDL_GPU texture and compositing paths are
   stable.
7. Promote `gpuapi` from shadow-upload diagnostics to a full presenter only
   after startup, touch, video, and fast-skip tests pass.

## Initial backend scope

- Supported upload formats: `R8`, `RGBA8`, `BGRA8`.
- Supported texture usage: sampler and optional color target.
- Out-of-bounds and oversized upload requests fail before touching SDL.
- The backend does not own windows or swapchains yet.
