# 2026-07-04 reference dirty completion continuation

## Trigger

User provided updated logs:

- `/root/log/78.log`
- `/root/log/20260703083213156.log`

User reported performance still below expectation: advancing text only has a few frames, advancing images would be worse.

## Log findings

The previous EGL partial-present patch did not activate on this device/config:

- `surface ready ... preserve=0`
- All sampled Android EGL presents remained full-frame:
  - `present-android-egl ... rect=0,0,1920x1080 ... fullFrame=1`
  - `present-texture-egl ... dirty=0,0,1920x1080 ... fullFrame=1`
- Approximate nativeGL present rate from `20260703083213156.log`:
  - `#1` at `08:32:25.569`
  - `#256` at `08:32:40.467`
  - about 17 FPS.
- Input queue pressure is high:
  - max input age reaches around `885ms`.
- Hot/default logs were still visible:
  - `LayerTree#FIRST` dumps many layer nodes.
  - `Motion resource manager load: title_bg.mtn`
  - `PSB lazy-load archive`
  - many `CollectLayers`, `layer: [...]`, and `ExtractFrameInfo` info lines.

## Reference project comparison

### krkrsdl2-main

Relevant model:

- `TVPWindowWindow::GetTVPSDLBitmapCompletion()` marks `needsGraphicUpdate`.
- `TVPWindowWindow::TickBeat()` consumes `bitmapCompletion->update_rect`.
- It calls `SDL_UpdateTexture(texture, &rect, surface->pixels, surface->pitch)`.
- It then renders either the dirty rect or full texture depending on zoom/full-update compile mode.

Important idea borrowed:

- Dirty ownership should come from bitmap/layer completion, not from final presenter guessing.
- The completion dirty rect must be attached to the final window texture/update path.

### krkrsdl3-main

Relevant model:

- `MainWindowLayer::UpdateDrawBuffer()` directly gets texture data and pitch.
- `krkrsdl_gl.cpp::SDL_GL_UpdateTexture()` uses `GL_UNPACK_ROW_LENGTH = pitch / 4` and `glTexSubImage2D`.
- Main loop is simple: run app, recycle textures, draw current sprite/overlay, swap.

Important idea borrowed:

- Avoid Cocos sprite fallback where possible.
- Window texture upload/present should be direct and pitch-aware.

## Root cause refined

Two separate issues were found.

### 1. Completion dirty was not attached to draw buffer texture

`tTVPBasicDrawDevice` already received dirty-like completion callbacks:

- `StartBitmapCompletion`
- `NotifyBitmapCompleted`
- `EndBitmapCompletion`

But the code only recorded completion data to SDL diagnostics:

- `TVPSDLRecordBitmapCompletionStart`
- `TVPSDLRecordBitmapCompletionRegion`
- `TVPSDLRecordBitmapCompletionEnd`

It did not mark the draw buffer texture dirty from those completion rectangles. Therefore `Show()->UpdateDrawBuffer(tex)` still handed the presenter a texture whose dirty state could remain full-frame from creation/resize/other paths.

### 2. GPU layer window completion always used the whole layer rect

In `tTJSNI_BaseLayer::CompleteForWindow()`:

```cpp
if(IsGPU()) {
    InternalComplete2_GPU(Rect, drawable);
}
```

This forced GPU completion to draw the full layer every time. The non-window `InternalComplete()` path already used:

```cpp
InternalComplete2_GPU(updateregion.GetBound(), drawable);
```

Also, the GPU path did not clear `updateregion`, unlike the CPU `InternalComplete2()` path. This could keep accumulating old update regions and eventually collapse to full-frame dirty.

## Patch made

Files edited:

- `cpp/core/visual/impl/BasicDrawDevice.h`
- `cpp/core/visual/impl/BasicDrawDevice.cpp`
- `cpp/core/visual/LayerIntf.cpp`
- `cpp/core/visual/LayerManager.cpp`
- `cpp/plugins/psbfile/PSBMedia.cpp`
- `cpp/plugins/motionplayer/ResourceManager.cpp`

### BasicDrawDevice dirty union

Added:

- `CompletionDirty`
- `CompletionDirtyRect`

Behavior:

- `StartBitmapCompletion()` clears the pending completion dirty rect.
- `NotifyBitmapCompleted()` unions the destination completion rect clipped to source size.
- `EndBitmapCompletion()` marks `manager->GetDrawBuffer()->GetTexture()` dirty with the union rect.

This follows the krkrsdl2-style dirty completion model while preserving the existing draw buffer pipeline.

### LayerManager DrawCompleted dirty marking

Follow-up analysis found that the active path in `tTVPLayerManager::DrawCompleted()` does not call:

```cpp
LayerTreeOwner->NotifyBitmapCompleted(...)
```

That branch is disabled by `#if 0`; the active code writes directly into `DrawBuffer`:

```cpp
DrawBuffer->Blt(...)
```

Therefore `BasicDrawDevice::NotifyBitmapCompleted()` is not enough by itself. The real dirty source must also be attached at the actual draw-buffer write site.

The patch now marks the draw buffer texture dirty after a successful `DrawBuffer->Blt(...)` using the same destination rectangle model as krkrsdl2:

```cpp
tTVPRect dirty(destrect.left, destrect.top,
               destrect.left + cliprect.get_width(),
               destrect.top + cliprect.get_height());
texture->MarkDirtyRect(dirty);
```

This is intentionally simple:

- no pixel readback
- no image integrity validation
- no per-pixel checks
- no presenter guessing

It mirrors the reference idea: dirty belongs to the completion/write path.

### GPU layer completion now uses update region bound

Changed `tTJSNI_BaseLayer::InternalComplete()`:

- If GPU and `updateregion.GetCount() > 0`, call `InternalComplete2_GPU(updateregion.GetBound(), drawable)`.
- Clear `updateregion` afterward.

Changed `tTJSNI_BaseLayer::CompleteForWindow()`:

- If GPU, use `Manager->GetUpdateRegionForCompletion().GetBound()` instead of full `Rect`.
- Clear the update region afterward.
- If the update region is empty but no draw buffer exists yet, draw the full layer once as a first-frame/resize fallback.
- If `Manager` is unexpectedly null, fallback to old full `Rect` behavior.

Expected result:

- If text updates only dirty a message-window/text area, final texture dirty should no longer be forced to full-screen solely by GPU layer completion.
- If the game truly invalidates full-screen during transitions or image changes, full-frame remains correct.

### Hot logging reductions

Default logging was reduced:

- `BasicDrawDevice` `KR2_RLOG` now writes only when `TVPSDLIsRenderDiagnosticsEnabled()` is true.
- `LayerManager` layer-tree diagnostics now run only when render diagnostics are enabled.
- `PSBMedia` layer collection/ExtractFrameInfo logs are now behind `KRKR2_ENABLE_PSB_MEDIA_DIAGNOSTICS`.
- `PSB lazy-load archive` changed from info to debug.
- `motion::ResourceManager` routine logs changed from info/warn to debug:
  - constructor `kag/cacheSize`
  - `setEmotePSBDecryptSeed`
  - `.mtn` load notice

## What next logs should show

Look for:

- No default `LayerTree#FIRST` spam unless diagnostics are explicitly enabled.
- No default `CollectLayers`, `ExtractFrameInfo`, `PSB lazy-load archive`, or `.mtn` load spam.
- `present-texture-egl` dirty rect should become smaller than full-screen during simple text advance if the game/layers only dirty text/message regions.
- On this device EGL may still report `preserve=0`; in that case EGL drawing remains full-frame for correctness, but upstream dirty and input pressure should still improve because GPU completion and logging work are reduced.

If `dirty=0,0,1920x1080` remains for text-only frames, next suspects:

- The game script/layer manager genuinely invalidates the whole primary layer each text advance.
- A public/manual render target path still marks the whole draw buffer dirty.
- Motion/title background plugin is animating or forcing full background invalidation while text is advancing.

## Reference project notes from sub-agents

### Best code to borrow next

- AetherKiri CPU/GPU dual residency:
  - `/root/kiriki-work/AetherKiri/cpp/core/visual/godot/GodotRenderManager.{h,cpp}`
  - key functions: `GodotTexture2D::Update`, `EnsureGpuHandle`, `UploadCpuToGpu`, `EnsureCpuReadable`, `CopyGpuFrom`, `BlendGpuFrom`, `GodotRenderManager::OperateRect`
- AetherKiri host presenter/final frame model:
  - `/root/kiriki-work/AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp`
  - key functions: `StoreLatestCpuFrameFromTexture`, `HostWindowLayer::UpdateDrawBuffer`, `TVPHostGetLatestFrameDesc`, `TVPHostCopyLatestFrameRGBA`
- krkrsdl3 pitch-aware GL upload:
  - `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`
  - key function: `SDL_GL_UpdateTexture`
  - important line of thought: `GL_UNPACK_ROW_LENGTH = pitch / 4`, then `glTexSubImage2D`, then restore row length.
- krkrsdl2 dirty completion:
  - `/root/kiriki-work/krkrsdl2-main/src/core/sdl2/SDLBitmapCompletion.{h,cpp}`
  - `/root/kiriki-work/krkrsdl2-main/src/core/sdl2/SDLApplication.cpp`
  - key functions: `TVPSDLBitmapCompletion::NotifyBitmapCompleted`, `TVPWindowWindow::TickBeat`

### Current project SDL3/GPU base already present

- `/root/kiriki-work/KiriKiri-LauncherC/cpp/core/render/sdlgpu/SDLGpuBackend.cpp`
  - `Backend::UploadTexture2D` is already pitch-aware.
  - It copies rows into an SDL_GPU transfer buffer and sets `pixels_per_row`.
- `/root/kiriki-work/KiriKiri-LauncherC/cpp/core/render/sdlgpu/SDLGpuTvpAdapter.cpp`
  - `UploadTexture2D` reads from `source.GetScanLineForRead(rect.top)`.
  - For non-RGB sources it offsets to `rect.left` and passes full `source.GetPitch()`.
  - For RGB sources it expands only the dirty rectangle into RGBA.
- `/root/kiriki-work/KiriKiri-LauncherC/cpp/core/render/sdlgpu/SDLGpuTextureCache.cpp`
  - `TextureCache::Upsert` accepts a dirty `sourceRect`, merges it into `cpuDirtyRect`, and records full vs partial uploads.
- `/root/kiriki-work/KiriKiri-LauncherC/cpp/core/environ/sdl/SDLGameManager.cpp`
  - `TVPSDLTryPresentTexture` calls `texture->PeekDirtyRect(updateRect)` and passes dirty to `TextureCache::Upsert`.

### Cocos replacement boundaries

Still-direct Cocos paths:

- Runtime:
  - `platforms/android/cpp/krkr2_android.cpp`: `cocos_android_app_init()`
  - `cpp/core/environ/cocos2d/AppDelegate.cpp`
  - `cpp/core/environ/cocos2d/CocosRuntimeHost.cpp`
- Render/window:
  - `cpp/core/environ/cocos2d/MainScene.cpp`
  - `TVPWindowLayer::UpdateDrawBuffer`
  - `TVPCreateAndAddWindow`
- Input:
  - `TVPMainScene::initialize`
  - `onKeyPressed`, `onKeyReleased`, `onTouchBegan`, etc.
  - Android JNI fallbacks in `platforms/android/cpp/krkr2_android.cpp`
- Android Activity:
  - `platforms/android/app/java/org/tvp/kirikiri2/KR2Activity.java`
  - `platforms/android/app/java/org/github/krkr2/MainActivity.kt`

Replacement order should stay:

1. Split out a non-Cocos `iWindowLayer` using the SDL/Flutter presenter path.
2. Make `UpdateDrawBuffer` presenter-only for the new host path.
3. Replace `TVPCocosRuntimeHost` with an SDL/Flutter runtime host.
4. Move Android input dispatch from Cocos `GLView/EventDispatcher` to the SDL direct queue.
5. Replace `KR2Activity extends Cocos2dxActivity` after render/input no longer require it.
