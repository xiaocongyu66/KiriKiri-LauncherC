# 2026-07-01 Temporary Notes: krkrsdl2/krkrsdl3 Reference

This is a quick recovery/learning note. Read this when continuing the
Flutter + SDL3 migration and when deciding what to copy from the SDL reference
projects.

## Current target

- Project to edit: `/root/kiriki-work/KiriKiri-LauncherC`.
- Do not edit reference projects.
- Hard target from user: migrate to Flutter + SDL3, gradually remove Cocos.
- Quality bar: complete, high-performance, high-compatibility.
- Avoid heavy validation in render hot paths. Prefer reference-proven state and
  format routing.

## Latest pushed commits

- `7d0b15d Fix GL texture layout and surface resize presents`
  - Fixed OpenGL RGB static texture scaling path.
  - Fixed Cocos fallback texture rect/scale refresh when adapter texture is
    reused.
  - Forced full-frame present after Flutter/Android surface size/lifecycle
    changes to avoid temporary black blocks in direct fallback path.
- `adbcee8 Record runtime presenter frame geometry`
  - Added runtime presenter frame snapshot.
  - Records source rect, destination rect, full-frame flag, native GL flag, and
    CPU-copy-free flag after successful Android present.
  - `RuntimeRenderManager` description now includes latest frame geometry.

CI state when this note was written:

- `adbcee8` format check passed.
- `adbcee8` Android build was in progress.
- Previous `7d0b15d` Android build was still in progress at the last check.

## User's latest rendering description

- Top layer is the correct layer and is positioned correctly, but appears
  gray/white instead of colorful.
- Lower colorful image/layer is visible but is not the correct top layer; it
  appears offset and can show outside the expected area.
- This suggests separate texture/layer paths:
  - one correct layer losing color;
  - another colorful layer or stale image with wrong positioning.
- Do not treat this as only a final SurfaceTexture shader/swap issue.
- OpenGL + Vulkan/GL presenter performance is good and stable.
- Software rendering is still slow.

## What was fixed for that issue

File: `cpp/core/visual/ogl/RenderManager_ogl.cpp`

- Old issue: `TVPShrinkImage()` treated every non-RGBA format as 8-bit
  grayscale.
- That is wrong for `TVPTextureFormat::RGB`; RGB must be resized as 3-channel
  data.
- Current fix:
  - `TVPShrink_RGB()` uses `CV_8UC3`.
  - `TVPShrinkImage()` dispatches:
    - RGBA -> `TVPShrink`
    - RGB -> `TVPShrink_RGB`
    - Gray -> `TVPShrink_8`
  - `CreateStaticTexture2D()` calls `TVPShrinkImage()`.
  - `TVPCheckOpaqueRGBA()` only runs for RGBA.

File: `cpp/core/environ/cocos2d/MainScene.cpp`

- Cocos fallback still exists during migration.
- Previous risk: if `tex2d == newtex`, `UpdateDrawBuffer()` skipped
  `setTextureRect()` and layout reset, so old texture rect/scale could survive
  after adapter texture reuse.
- Current fix:
  - Added `_drawTextureRectWidth/_drawTextureRectHeight`.
  - Added `UpdateDrawSpriteTextureLayout(iTVPTexture2D*)`.
  - It recalculates scale and clamps visible rect to internal texture size.
  - It is called whether adapter texture changes or is reused.
  - Reused texture path also calls `ResetDrawSprite()`.

File: `cpp/core/environ/sdl/SDLGameManager.cpp`

- Direct CPU fallback can show temporary black blocks after Flutter
  SurfaceTexture/ANativeWindow resize because partial dirty rects are posted
  into a fresh buffer.
- Current fix:
  - `gSDLAndroidForceFullFramePresent`.
  - Set on `TVPSDLNotifyAndroidFlutterGameSurfaceChanged()`.
  - Set when presented surface size changes.
  - `TVPSDLTryPresentTexture()` consumes it once and expands next dirty rect
    to the full texture.

## krkrsdl2 useful pieces

Reference root: `/root/kiriki-work/krkrsdl2-main`.

Important files:

- `src/core/sdl2/SDLApplication.cpp`
- `src/core/sdl2/SDLBitmapCompletion.cpp`
- `src/core/visual/sdl2/BasicDrawDevice.cpp`

Copy the ideas, not necessarily exact code:

- `TVPSDLBitmapCompletion`
  - Receives completed layer bitmap regions.
  - Handles bottom-up/top-down bitmap orientation.
  - Copies into an SDL surface.
  - Unions dirty rects with `update_rect`.
- `TVPWindowWindow::TickBeat()`
  - If dirty, updates SDL texture and presents.
  - Zoom mode uses full source rect plus destination rect.
  - Non-zoom mode can present dirty rects directly.
- `LastSentDrawDeviceDestRect`
  - This is very important.
  - It is the stable model for destination rectangle ownership.
  - It is also used for input coordinate reverse mapping.

Do not blindly copy:

- SDL2 calls `SDL_UpdateTexture(texture, &rect, surface->pixels,
  surface->pitch)`.
- For our implementation, dirty upload should use explicit offset:
  `base + rect.y * pitch + rect.x * bytesPerPixel`.
- Keep `pitch` as full image pitch and use row-length logic where available.

## krkrsdl3 useful pieces

Reference root: `/root/kiriki-work/krkrsdl3-main`.

Important files:

- `cpp/krkrsdl.cpp`
- `cpp/krkrsdl_gl.cpp`
- `cpp/eventCallbackFun.h`
- `cpp/environ/MainWindowLayer.cpp`
- `cpp/core/render/DrawDevice.cpp`
- `cpp/core/render/RenderManagerGL.cpp`

Good architecture to copy:

- SDL3 callback lifecycle:
  - `SDL_AppInit`
  - `SDL_AppEvent`
  - `SDL_AppIterate`
  - `SDL_AppQuit`
- `SDL_Sprite` presenter concept:
  - `texture`
  - `xPos`
  - `yPos`
  - `scale`
  - `width`
  - `height`
  - `isVisible`
- `SDL_GL_DrawTexture()`
  - Computes contain/letterbox scale:
    `scale = min(windowW / spriteW, windowH / spriteH)`.
  - Stores `xPos/yPos/scale` on the sprite.
  - Those values are used by input mapping.
- Input reverse mapping:
  - mouse/touch coordinates are converted with:
    `(hostX - sprite.xPos) / sprite.scale`
    `(hostY - sprite.yPos) / sprite.scale`
- `SDL_GL_UpdateTexture()`
  - Uses `GL_UNPACK_ROW_LENGTH = pitch / 4`.
  - Uses `GL_UNPACK_ALIGNMENT = 1`.
  - Uploads with `glTexSubImage2D(... GL_RGBA, GL_UNSIGNED_BYTE, buff)`.

Do not blindly copy:

- krkrsdl3's current final presenter uploads full frame.
- For mobile performance, borrow dirty rect union from krkrsdl2 and use
  partial `glTexSubImage2D` when safe.
- krkrsdl3 mostly assumes RGBA/Gray and modern GL formats such as
  `GL_R8/GL_RED`.
- LauncherC still needs AetherKiri-style compatibility:
  - RGB path
  - Gray path
  - RGBA path
  - GLES2-safe `GL_LUMINANCE` where required
  - no unsupported `GL_RED` without version/shader handling

## AetherKiri still matters

Keep using AetherKiri for Android EGL/SurfaceTexture lifecycle:

- attach native window
- detach native window
- update native window size
- mark frame dirty
- consume dirty before swap
- swap only when a real frame is ready

The user explicitly prefers stable reference implementations over novel
experiments. AetherKiri is the strongest reference for Android EGL lifecycle.

## Current runtime architecture direction

Already present in LauncherC:

- `RuntimeHost`
- `RuntimePresenter`
- `RuntimeRenderManager`

Newly added:

- `TVPRuntimePresentFrameInfo`
- `TVPRuntimeRecordPresentFrame()`
- `TVPRuntimeGetPresentFrameInfo()`

This is the beginning of moving presenter geometry out of Cocos.

Next module to create:

- `cpp/core/environ/sdl/SDLAndroidEGLPresenter.h`
- `cpp/core/environ/sdl/SDLAndroidEGLPresenter.cpp`

Suggested responsibilities:

- Own Android EGL/Flutter SurfaceTexture presenter state.
- Own `ANativeWindow` attach/detach.
- Own EGL surface recreate.
- Own source rect and destination rect.
- Own full-frame-after-resize flag.
- Own frame dirty/swap gate.
- Publish `TVPRuntimePresentFrameInfo`.

Keep `SDLGameManager.cpp` as coordinator only.

## Next practical implementation checklist

1. Extract Android EGL presenter state from `SDLGameManager.cpp`.
2. Keep function names close to AetherKiri:
   - `AttachNativeWindow`
   - `DetachNativeWindow`
   - `UpdateNativeWindowSize`
   - `MarkFrameDirty`
   - `ConsumeFrameDirty`
3. Move `gSDLAndroidForceFullFramePresent` into that presenter module.
4. Move EGL program/texture/window state into that presenter module.
5. Add presenter layout:
   - source rect
   - destination rect
   - contain/letterbox scale
6. Use that layout for input reverse mapping later.
7. Keep Cocos fallback working until SDL3/Flutter path has parity.

## Commands that worked locally

```sh
cd /root/kiriki-work/KiriKiri-LauncherC
git diff --check
python3 -m json.tool vcpkg.json >/tmp/kiriki-vcpkg-json-check.txt
```

Local missing tools historically:

- `cmake`
- `java`
- `flutter`
- `dart`
- `clang-format`
- `gh`

GitHub Actions is authoritative for Android builds.
