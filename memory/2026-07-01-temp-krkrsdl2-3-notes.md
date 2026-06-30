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

## 2026-07-01 fast scene block artifact investigation

Newest logs inspected:

- `/root/log/20260701030817384.log`
- `/root/log/20260701030915559.log`
- `/root/log/20260701030946970.log`
- `/root/log/78.log`

Important conclusion:

- The high-performance Android EGL path is continuously presenting full frames.
  Examples from the new logs show `present-android-egl` and
  `present-texture-egl` with `dirty=0,0,1920x1080` and `fullFrame=1`.
- Therefore the user's fast-scene black/missing rectangular blocks are unlikely
  to be caused by final Flutter SurfaceTexture dirty-rect posting in the EGL
  path.
- The direct fallback path can still present partial rectangles, for example
  `dirty=0,867,1920x67` in `78.log`. That remains relevant to software/direct
  fallback, but the currently preferred OpenGL + EGL/Vulkan-performing path is
  already full-frame at the final presenter.

Code-level issue found in LauncherC OpenGL render manager:

- File: `cpp/core/visual/ogl/RenderManager_ogl.cpp`.
- `TVPRenderManager_OpenGL` used a single reusable `tempTexture` for all
  target-as-source copies inside one `OperateRect()` / `OperateTriangles()`.
- The code correctly avoids direct framebuffer feedback by copying the target
  texture to a temp texture when an input texture is the same object as the
  render target, and when a method has `tar_as_src`.
- However, a single draw can need more than one target snapshot:
  - one or more entries in `textures[]` can be `tex == tar`;
  - the render method itself can also have `tar_as_src`.
- With a single temp texture, the later snapshot overwrites the same GL texture
  object that earlier `GLVertexInfo` entries still reference. Under fast layer
  transitions/effects, that can make a rectangle sample from the wrong target
  snapshot and visually appear as a stale next/previous layer block or black
  block.

Implemented fix:

- Replaced the single `tempTexture` member with `tempTextures`, a small vector
  of reusable temp textures.
- `GetTempTexture2D(src, rc, slot)` now returns a temp texture for a specific
  slot and resizes/reuses that slot independently.
- Each self-target source in `OperateRect()` consumes one temp slot.
- The `tar_as_src` snapshot in the same draw consumes the next temp slot.
- `OperateTriangles()` uses the same slot model.
- This keeps the proven AetherKiri-style target snapshot design but removes
  the single-slot overwrite hazard. Normal non-self-referencing draws still do
  not allocate or copy extra textures.

Why this fits the user's performance requirement:

- It does not add per-pixel validation, readback checks, or heavyweight debug
  guards in the render hot path.
- It only allocates extra temporary GPU textures when a draw truly needs
  multiple target snapshots.
- It preserves the CPU-copy-free Android EGL presenter path and targets the
  upstream GL composition artifact instead of masking it in the final presenter.

Next checks after a CI/device build:

- Re-test the fast scene changes where character head/top-left blocks appeared.
- Confirm logs still show EGL path with `fullFrame=1`, `nativeGL=5`,
  `softwareUpload=0`, and `cpu-copy-free`.
- If artifacts remain, inspect per-effect methods that set `tar_as_src`,
  especially regular blend paths when framebuffer-fetch is unavailable, and
  transition effects in `TransIntf.cpp`.

## 2026-07-01 direct Flutter fallback full-frame policy

Subagent log analysis found that the non-EGL/direct run is the suspicious one:

- `20260701030915559.log` uses `reason=no-native-gl`, then
  `present-flutter-direct`.
- Later frames can be partial, for example `dirty=0,867,1920x67`.
- The EGL runs remain full-frame.

Implemented compatibility fix:

- File: `cpp/core/environ/sdl/SDLGameManager.cpp`.
- Added `KRKR2_ANDROID_DIRECT_PARTIAL_PRESENT=1` diagnostic switch.
- By default, `TryPresentAndroidFlutterTexture()` and
  `TryPresentAndroidFlutterSurface()` expand the lock/copy/post rectangle to
  the full surface.
- `TVPSDLTryPresentTexture()` records the actual full-frame direct fallback
  rectangle in `TVPRuntimePresentFrameInfo` and in `present-texture-direct`
  logs.

Reasoning:

- Android `ANativeWindow_lock()` dirty rectangles are not a good correctness
  foundation for this hybrid renderer. A partial dirty lock can preserve
  unknown/stale content from another buffer in the queue, which matches the
  user's "one block from previous/next layer" description.
- This fallback is already the slower compatibility path. Correctness is more
  important there than partial-copy optimization.
- The preferred high-performance path remains Android EGL/SurfaceTexture with
  native GL textures and full-frame GPU presentation.
- Own source rect and destination rect.
- Own full-frame-after-resize flag.
- Own frame dirty/swap gate.
- Publish `TVPRuntimePresentFrameInfo`.

Keep `SDLGameManager.cpp` as coordinator only.

## Next practical implementation checklist

1. Extract `SDLAndroidFlutterPresenter.{h,cpp}` first.
   - Move Android EGL state and helpers out of `SDLGameManager.cpp`.
   - Move direct Flutter `ANativeWindow_lock` CPU fallback with it.
   - Move `TVPSDLNotifyAndroidFlutterGameSurfaceChanged()`.
   - Preserve behavior: EGL first, direct CPU fallback second, dirty consumed
     only after successful present.
2. Keep function names close to AetherKiri:
   - `AttachNativeWindow`
   - `DetachNativeWindow`
   - `UpdateNativeWindowSize`
   - `MarkFrameDirty`
   - `ConsumeFrameDirty`
3. Move `gSDLAndroidForceFullFramePresent` into that presenter module.
4. Move EGL program/texture/window state into that presenter module.
5. Add `SDLPresentTypes.h` / a texture present pipeline after the Android
   presenter is isolated.
   - Keep `TVPSDLTryPresentTexture()` as the stable entrypoint.
   - Internally route Android EGL, direct CPU fallback, surface mirror, then
     SDL window fallback.
6. Extract SDL surface mirror state.
7. Extract SDL window/screen presenter state.
8. Add presenter layout:
   - source rect
   - destination rect
   - contain/letterbox scale
9. Use that layout for input reverse mapping later.
10. Keep Cocos fallback working until SDL3/Flutter path has parity.

Implemented first slice:

- Added `cpp/core/environ/sdl/SDLPresentTypes.h`.
- Added:
  - `TVPSDLPresentPath`
  - `TVPSDLPresentRect`
  - `TVPSDLTexturePresentPlan`
  - `TVPSDLTexturePresentResult`
- Updated Android branch inside `TVPSDLTryPresentTexture()` to build a
  `TVPSDLTexturePresentPlan` and record a `TVPSDLTexturePresentResult`.
- This keeps behavior the same but removes several scattered local booleans and
  `SDL_Rect` variables from the orchestration path.
- Added internal `TryPresentAndroidTexturePlan()` in `SDLGameManager.cpp`.
  This helper tries Android EGL first, then direct Flutter CPU fallback, and
  fills `TVPSDLTexturePresentResult`.
- The next extraction can make `SDLAndroidFlutterPresenter` return
  `TVPSDLTexturePresentResult` directly.

CMake reminder:

- `cpp/core/environ/CMakeLists.txt` explicitly lists `.cpp` files.
- Header-only `SDLPresentTypes.h` required no CMake change.
- When adding `SDLAndroidFlutterPresenter.cpp`, it must be added beside
  `sdl/SDLGameManager.cpp`, `sdl/SDLRuntimePresenter.cpp`, and
  `sdl/SDLUIManager.cpp`.

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

## 2026-07-01 follow-up: OpenGL artifact and Cocos-removal pass

User-reported current state:

- OpenGL render pipeline + Vulkan/GL presentation is now fast and stable.
- Software renderer remains slow, around 10 FPS.
- OpenGL still has a main-menu background issue:
  - top/correct layer can appear gray/white;
  - lower colorful layer exists but is offset and does not cover correctly;
  - after colorful layer appears, top layer becomes the corresponding gray
    layer;
  - fast scene changes can still show missing character blocks or transient
    black blocks.
- User emphasized not to add excessive validation or graphical integrity checks
  in hot paths. Prefer proven reference behavior and lean state management.

Reference/subagent findings:

- AetherKiri already removed Cocos viewport/event dependencies from its OpenGL
  render manager.
  - `_RestoreGLStatues()` resets pixel store row length and render target, then
    leaves default framebuffer viewport ownership to the active runtime host.
  - renderer recreation uses `krkr::gl::OnRendererRecreated(...)` instead of
    Cocos `EVENT_RENDERER_RECREATED`.
  - `AdapterTexture2D` uses a local `krkr::Texture2D` wrapper, not
    `cocos2d::Texture2D`.
- krkrsdl2 provides useful coordinate/layout semantics, especially treating the
  destination rect as presenter-owned state and converting input through it.
- krkrsdl3 provides useful GL upload semantics:
  - set `GL_UNPACK_ROW_LENGTH` and `GL_UNPACK_ALIGNMENT` before upload;
  - restore row length/alignment after upload;
  - keep GL default framebuffer/viewport setup owned by the SDL host.
- Current `RenderManager_ogl.cpp` suspicious points:
  - Cocos adapter path still wraps GL texture IDs in `cocos2d::Texture2D`.
  - `TVPSetRenderTarget()` keeps global FBO cache state; external host/presenter
    GL calls can make that cache stale if not invalidated.
  - `InitPixel()` had a concrete bug: it called `glTexImage2D()` before setting
    `GL_UNPACK_ALIGNMENT`.
  - `BlendResetToCache()` in the local `krkr_gl` wrapper is intentionally a
    no-op because current code calls raw GL blend state directly.
  - `FireRendererRecreated()` is not wired to Android/SDL GL lifecycle yet.

Implemented in this pass:

- File: `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  - Removed `renderer/ccGLStateCache.h` include after replacing
    `cocos2d::GL::*` calls with `krkr::gl::*`.
  - Removed Cocos Director/EventDispatcher/EventType includes.
  - Replaced debug extension logging from `cocos2d::log()` to `TVPConsoleLog()`.
  - Changed `_RestoreGLStatues()` to stop calling
    `cocos2d::Director::getInstance()->setViewport()`.
    The active runtime host now owns the default framebuffer viewport, matching
    AetherKiri.
  - Replaced Cocos renderer-recreated listener with:
    `krkr::gl::OnRendererRecreated([this] { clear/rebuild methods; })`.
  - Fixed `tTVPOGLTexture2D_static::InitPixel()` so
    `GL_UNPACK_ALIGNMENT` is set before `glTexImage2D()`.

Why these changes are low risk:

- They follow AetherKiri's already-tested structure.
- They remove Cocos state intervention from GL restore/recreate boundaries.
- They avoid adding per-frame validation or expensive diagnostics.
- The `InitPixel()` fix changes only upload ordering for a path that was
  previously objectively wrong.

Remaining Cocos usage in `RenderManager_ogl.cpp` after this pass:

- `#include "renderer/CCTexture2D.h"`
- `#include "renderer/CCGLProgramCache.h"`
- `#include "renderer/CCGLProgram.h"`
- `AdapterTexture2D : public cocos2d::Texture2D`
- `setGLProgram(cocos2d::GLProgramCache::getInstance()->getGLProgram(...))`
- `GetAdapterTexture(cocos2d::Texture2D *orig)`

Important interface constraint:

- Current LauncherC `cpp/core/visual/RenderManager.h` still exposes
  `cocos2d::Texture2D *GetAdapterTexture(cocos2d::Texture2D *origTex)`.
- Cocos host/UI still consume that type:
  - `cpp/core/environ/cocos2d/MainScene.cpp`
  - `cpp/core/environ/ui/DebugViewLayerForm.cpp`
  - software `cpp/core/visual/RenderManager.cpp`
- AetherKiri changed this boundary to `krkr::Texture2D`. That migration must be
  done across `RenderManager.h`, software renderer, OGL renderer, Cocos host
  compatibility shims, and debug UI together. Do not half-change only
  `RenderManager_ogl.cpp`.

Next safest implementation sequence:

1. Add/port `cpp/core/visual/ogl/krkr_texture2d.h` from AetherKiri.
2. Change `RenderManager.h` adapter type from `cocos2d::Texture2D` to
   `krkr::Texture2D`, but only if all call sites are updated in the same patch.
3. Update software `RenderManager.cpp` to use the local texture wrapper and
   remove `renderer/CCTexture2D.h` from core visual.
4. Update `RenderManager_ogl.cpp` `AdapterTexture2D` to inherit
   `krkr::Texture2D` and remove `setGLProgram()`.
5. Update Cocos-only consumers by adding a small bridge in the legacy Cocos host
   if needed, so new Flutter/SDL3 path no longer depends on Cocos texture types.
6. Wire `krkr::gl::FireRendererRecreated()` into Android/SDL GL context
   recreation when the new host owns that lifecycle.
7. Extract `SDLAndroidFlutterPresenter.{h,cpp}`:
   - move Android EGL/direct state out of `SDLGameManager.cpp`;
   - keep dirty rect consumption in `TVPSDLTryPresentTexture()`;
   - keep EGL-first/direct-fallback behavior;
   - keep direct fallback full-frame by default.

Checks run after this pass:

```sh
cd /root/kiriki-work/KiriKiri-LauncherC
git diff --check
```

`git diff --check` passed.

## 2026-07-01 follow-up: renderer texture adapter boundary

Problem:

- AetherKiri migrated the renderer adapter type from `cocos2d::Texture2D` to
  `krkr::Texture2D`.
- LauncherC could not directly copy AetherKiri's standalone `Texture2D` yet,
  because the legacy Cocos host still does:
  - `Texture2D *newtex = tex->GetAdapterTexture(tex2d);`
  - `DrawSprite->setTexture(newtex);`
- If `krkr::Texture2D` became a standalone class immediately, the legacy host
  would stop compiling before Flutter + SDL3 host fully owns presentation.

Implemented transitional boundary:

- Added `cpp/core/visual/ogl/krkr_texture2d.h`.
- This header is currently the only Cocos texture compatibility point in
  core visual:
  - `krkr::Texture2D` aliases `cocos2d::Texture2D`.
  - `krkr::PixelFormat` aliases `cocos2d::Texture2D::PixelFormat`.
  - `krkr::Size` aliases `cocos2d::Size`.
  - `krkr::SetDefaultTextureProgram()` hides the legacy Cocos shader setup for
    adapter textures.
- Updated `cpp/core/visual/RenderManager.h`:
  - `GetAdapterTexture()` now uses `krkr::Texture2D *` in the renderer
    interface.
  - The header only forward-declares `cocos2d::Texture2D` and adds a protected
    `krkr::Texture2D` alias; it does not include Cocos headers.
- Updated `cpp/core/visual/RenderManager.cpp`:
  - removed direct `renderer/CCTexture2D.h` include;
  - software texture adapter code now uses `krkr::Texture2D`,
    `krkr::PixelFormat`, and `krkr::Size`;
  - preserved existing dirty rect incremental upload logic.
- Updated `cpp/core/visual/ogl/RenderManager_ogl.cpp`:
  - removed direct `CCTexture2D`, `CCGLProgram`, and `CCGLProgramCache`
    includes;
  - `AdapterTexture2D` now inherits `krkr::Texture2D`;
  - default shader setup goes through `krkr::SetDefaultTextureProgram()`;
  - the file now has no direct `cocos2d::` references.

Important interpretation:

- This is an interface migration and containment step, not final Cocos removal.
- Cocos texture dependency still exists in `krkr_texture2d.h` so the old host
  remains compatible.
- Once Flutter + SDL3 presentation no longer needs Cocos `Sprite::setTexture()`,
  replace the compatibility alias in `krkr_texture2d.h` with the standalone
  AetherKiri implementation.
- Do not remove the Cocos alias until the legacy host adapter call sites are
  either deleted, isolated behind `KRKR2_ENABLE_COCOS_HOST`, or bridged.

Current Cocos texture usage in core visual after this pass:

- `cpp/core/visual/ogl/krkr_texture2d.h` is the only file that includes Cocos
  texture/program headers.
- `RenderManager.h` has a forward alias to keep pointer signatures lightweight:
  `krkr::Texture2D = cocos2d::Texture2D`.
- `RenderManager.cpp` and `RenderManager_ogl.cpp` no longer expose or mention
  `cocos2d::Texture2D` directly.

Checks run:

```sh
cd /root/kiriki-work/KiriKiri-LauncherC
grep -n "cocos2d\\|CCTexture2D\\|CCGLProgram\\|ccGLStateCache" \
  cpp/core/visual/ogl/RenderManager_ogl.cpp \
  cpp/core/visual/RenderManager.cpp \
  cpp/core/visual/RenderManager.h \
  cpp/core/visual/ogl/krkr_texture2d.h
git diff --check
```

The grep shows Cocos texture/program includes only inside `krkr_texture2d.h`.
`RenderManager.h` only has the forward alias.
`git diff --check` passed.
