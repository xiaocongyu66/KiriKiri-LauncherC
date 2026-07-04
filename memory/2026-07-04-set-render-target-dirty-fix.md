# 2026-07-04 SetRenderTarget dirty fix and 78.log notes

## User-facing hard requirements

- Project to modify: `KiriKiri-LauncherC` only.
- Reference-only projects: `/root/kiriki-work/AetherKiri`, `/root/kiriki-work/docs`, `/root/kiriki-work/kirikiroid2-web`, `/root/kiriki-work/KrKr2-Next`, `/root/kiriki-work/krkrsdl2-main`, `/root/kiriki-work/krkrsdl3-main`, `/root/kiriki-work/SDL-release-3.4.10`.
- Hard target remains migration to Flutter + SDL3 and gradual removal of Cocos.
- Rendering must be complete, high-performance, and high-compatibility.
- Avoid adding render hot-path validation or "graphical integrity validation" that reference projects do not use.
- User explicitly supports log output switches.
- User clarified fixed resolution `1920x1080` is normal and supported; the issue is not the resolution itself.

## `/root/log/78.log` observations

- Relevant package is `org.github.krkr2`.
- Two relevant app processes appeared:
  - `20856`: OpenGL/nativeGL path.
  - `21196`: softwareUpload path.
- No native crash was visible for `org.github.krkr2`.
  - Both relevant runs ended with `System.exit called, status: 0`.
  - No `Fatal signal` or `FATAL EXCEPTION` tied to `org.github.krkr2` was found in the relevant run windows.
- The runtime is still Cocos-based at the outer window layer:
  - Logs include `Cocos2dxActivity` lifecycle traces and `WindowLayer::UpdateDrawBuffer`.
- EGL surface preservation:
  - `surface ready ... preserve=0`.
  - Final EGL presentation still has to draw a full frame for correctness when buffer preservation is unavailable.
  - This does not mean upstream upload/copy must be full-screen; dirty rect is still important before the final draw.
- NativeGL run:
  - First presenter logs:
    - `surface ready #1 ... size=1920x1080 preserve=0`
    - `present-android-egl #1 ... nativeGL=5 softwareUpload=0 ... fullFrame=1`
    - `present-texture-egl #1 ... dirty=0,0,1920x1080 ... nativeGL=1 cpuCopyFree=1 gpuFull=0 gpuPartial=0`
  - Later samples such as `#128` still report `dirty=0,0,1920x1080`.
- SoftwareUpload run:
  - `nativeGL=0 softwareUpload=1 cpuCopyFree=0`.
  - Presenter logs still report `dirty=0,0,1920x1080`.
  - Even when Android present rect became `0,58,1920x1022`, `present-texture-egl` still reported full texture dirty.
- The previous completion-dirty patch therefore did not reach the final presenter dirty boundary.

## Root cause identified in current code

File:

- `cpp/core/visual/ogl/RenderManager_ogl.cpp`

Problem:

- `TVPRenderManager_OpenGL::SetRenderTarget(iTVPTexture2D *target)` called:
  - `texture->AsTarget();`
  - `texture->MarkDirtyRect(full texture);`
- This means merely binding a render target marks the whole texture dirty.
- Any path that binds the window/draw-buffer texture before rendering can force presenter dirty to full-screen, even if the actual draw operation is small.

Why this is bad:

- It defeats dirty rect propagation.
- It makes `TVPSDLTryPresentTexture()` see `PeekDirtyRect() == fullRect`.
- It prevents pitch-aware partial upload / GPU shadow cache / CPU-GPU dual residency from helping on text-heavy or small-update scenes.
- It is especially harmful while final Android EGL has `preserve=0`, because final draw may be full-frame but upstream copy/upload should still be limited.

Why removing it is safe in the normal render paths:

- Real draw paths already mark dirty when they actually draw:
  - `OperateRect(...)` marks `tar->MarkDirtyRect(rctar)`.
  - `OperateTriangles(...)` marks `tar->MarkDirtyRect(rcclip)`.
  - `CopyTexture(...)` marks destination dirty.
  - texture upload/update paths mark their written rects.
  - pixel writes mark small rects or full only when truly needed.
- `SetRenderTarget()` should bind FBO/render target state only; it should not imply a color write.

Compatibility check performed:

- Motion-player paths that call `SetRenderTarget()` were checked:
  - `cpp/plugins/motionplayer/PrivateMotionGLL.cpp`
  - `cpp/plugins/motionplayer/PlayerRenderTargets.cpp`
- Those paths use `OperateTriangles()` for actual affine/mesh drawing, so dirty is still recorded at the real draw boundary.
- Stencil setup calls `SetRenderTarget()` to bind and clear stencil; this should not mark color dirty.

## Patch applied

Files changed:

- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  - Removed the full-texture `MarkDirtyRect()` from `TVPRenderManager_OpenGL::SetRenderTarget()`.
  - Now `SetRenderTarget()` only calls `texture->AsTarget()`.
- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Gated default presenter success sampling logs behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
  - Gated `prepresent-drop` input diagnostics behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
  - Kept failure/unavailable paths at the existing default logging behavior.
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Gated `present-android-egl` success sampling logs behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
  - Kept Android EGL failure/unavailable/auto-disable logs at the existing default logging behavior.
- `cpp/plugins/motionplayer/SourceCache.cpp`
  - Added `KRKR2_ENABLE_MOTION_SOURCE_DIAGNOSTICS`.
  - `SourceCache direct PSB bitmap: ...` info logs are now emitted only when that env var is truthy.
  - This reduces default hot-path logging during PSB/motion image loading without removing the diagnostic completely.

## Verification done locally

- `git diff --check` passed.
- Full native build was not run locally because prior environment lacks local C++ build tools (`cmake`, `ninja`, `clang++`, `g++`, `c++` were absent in earlier checks).
- CI/GitHub Actions should be used for compile verification after commit/push.

## Expected next test result

- New logs should still show `surface ready ... size=1920x1080 preserve=0`.
- `present-android-egl ... fullFrame=1` may remain because Android EGL surface preservation is unavailable.
- The important expected improvement is:
  - `present-texture-egl ... dirty=...` should stop being full-screen on small updates.
  - `gpuPartial` should start increasing when SDL_GPU shadow upload is active and texture cache already knows the texture.
  - Text advance scenes should avoid uploading/copying all `1920x1080` pixels when only a smaller area changes.

## If dirty is still full after this patch

Next likely sources to inspect:

- Full dirty from bitmap completion or layer update region:
  - `cpp/core/visual/LayerManager.cpp`
  - `cpp/core/visual/LayerIntf.cpp`
  - `cpp/core/visual/impl/BasicDrawDevice.cpp`
- Full dirty from texture resize/init:
  - `tTVPOGLTexture2D::SetSize`
  - initial texture creation and full upload paths.
- Presenter consumption order:
  - `cpp/core/environ/sdl/SDLGameManager.cpp`
  - `TVPSDLTryPresentTexture()` uses `PeekDirtyRect()` first, presents, then `ConsumeDirtyRect()`.
- Avoid adding heavy validation. If diagnostics are needed, use existing `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS` or a narrowly gated counter showing dirty source, not per-pixel checks.

## Relation to reference projects

- AetherKiri design direction:
  - Keep CPU/GPU dual residency and mark changes at actual operations.
  - Useful files:
    - `/root/kiriki-work/AetherKiri/cpp/core/visual/godot/GodotRenderManager.{h,cpp}`
    - `/root/kiriki-work/AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp`
- krkrsdl3 pitch-aware upload:
  - `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`
  - `SDL_GL_UpdateTexture` uses row pitch through `GL_UNPACK_ROW_LENGTH = pitch / 4`.
- krkrsdl2 dirty completion:
  - `/root/kiriki-work/krkrsdl2-main/src/core/sdl2/SDLBitmapCompletion.{h,cpp}`
  - Dirty is collected around completion/update, not at render-target bind time.

## Current subagents

Two read-only explorer subagents were spawned:

- One to independently evaluate the `SetRenderTarget()` full dirty removal against reference projects.
- One to review `78.log` hot-path logging and exception noise.

Both returned and confirmed the current direction:

- `SetRenderTarget()` should not mark dirty. Binding a render target is not a write. AetherKiri's OpenGL path follows this pattern, and krkrsdl2 dirty completion collects real update regions instead of treating bind as dirty.
- The main compatibility risk is external hand-written GL color drawing that calls `SetRenderTarget()` and then bypasses `OperateRect()` / `OperateTriangles()` / `CopyTexture()`. Current motionplayer paths checked use `OperateTriangles()` for actual color writes. Stencil setup is not a color write and should not dirty color.
- Log noise in `78.log`:
  - `SourceCache direct PSB bitmap`: 22 lines, now behind `KRKR2_ENABLE_MOTION_SOURCE_DIAGNOSTICS`.
  - OpenCV `glob_rec`: 3 lines from OpenCV internals trying to treat `base.apk!/lib/arm64-v8a` as a directory. Do not add a global OpenCV error redirect for this.
  - presenter success samples (`present-android-egl`, `present-texture-*`, `shadow-upload #`, `shadow-upload skipped`) should be diagnostics-only; now gated behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
  - `prepresent-drop` is startup/input diagnostic, not an exception; now gated behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
  - lifecycle/failure/unavailable logs should remain visible by default.

## Diagnostic switches after this patch

- `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS=1`
  - Re-enables presenter success samples such as `present-android-egl`, `present-texture-egl`, software/mirror `present-texture`, shadow-upload success/skip samples, and `prepresent-drop`.
  - Use this when collecting logs to verify dirty rect behavior.
- `KRKR2_ENABLE_MOTION_SOURCE_DIAGNOSTICS=1`
  - Re-enables `SourceCache direct PSB bitmap` lines.
  - Use only when debugging PSB/motion bitmap source resolution.

## Follow-up after user reported worse behavior

User then reported `78.log` behavior became more serious and FPS remained low. The important interpretation:

- Removing full dirty from `SetRenderTarget()` is still correct for the SDL2/SDL3-style architecture.
- However, current hybrid architecture still has external GL/native render paths and Cocos-era presentation assumptions.
- Some GL-backed frames can reach `TVPSDLTryPresentTexture()` without a CPU-upload dirty rect. If the presenter has already shown at least one frame, the old code returned success without presenting anything. This can make visible output stale even though the engine advanced.
- The fix should not restore bind-time full dirty, because that destroys dirty rect performance. The fix is to separate final GL presentation from CPU/GPU upload dirty.

Patch direction applied after this report:

- `cpp/core/environ/sdl/SDLPresentTypes.h`
  - Added `TVPSDLTexturePresentPlan::allowFallback`.
- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Added `nativePresentOnly` path:
    - only for takeover-active, already-presented, GL-backed textures with no dirty rect.
    - uses a full frame rect only for final Android EGL presentation.
    - does not invalidate CPU pixel cache.
    - does not run SDL_GPU shadow upload.
    - does not consume texture dirty.
    - does not fall back to software/direct Flutter full copy if EGL is unavailable.
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Honors `plan.allowFallback`; present-only frames can use EGL but cannot silently degrade into CPU-copy fallback.

Why this matches SDL2/SDL3 reference direction:

- SDL2 `TVPSDLBitmapCompletion` collects real dirty rectangles at bitmap completion and copies only those regions into an SDL surface.
- SDL3 `SDL_GL_UpdateTexture` uses pitch-aware texture upload (`GL_UNPACK_ROW_LENGTH`) and keeps upload semantics separate from final render.
- Our new `nativePresentOnly` path keeps that separation: no upload dirty is invented, but the final EGL surface can still be redrawn from an existing native GL texture when current hybrid layers did not produce dirty metadata.

Next migration steps:

- Keep moving window presentation out of Cocos `WindowLayer::UpdateDrawBuffer`.
- Prefer SDL runtime presenter ownership for the game surface.
- Continue reducing full-frame CPU readback paths.
- If new logs are needed, ask user to run with `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS=1` so `present-texture-*` and `present-android-egl` lines show dirty/full/nativeGL/present-only behavior.

## Follow-up for low FPS and SDL3/2 pitch-aware upload

User then emphasized FPS is still low and asked to continue migration while referencing SDL3/2 implementation. Applied next local direction:

- Reference:
  - `/root/kiriki-work/krkrsdl3-main/cpp/krkrsdl_gl.cpp`
  - `SDL_GL_UpdateTexture()` binds texture, sets `GL_UNPACK_ROW_LENGTH = pitch / 4`, sets `GL_UNPACK_ALIGNMENT = 1`, then calls `glTexSubImage2D`.
- Current project already has pitch-aware SDL_GPU upload:
  - `cpp/core/render/sdlgpu/SDLGpuTvpAdapter.cpp`
  - `cpp/core/render/sdlgpu/SDLGpuBackend.cpp`
  - These use `source.GetPitch()` and copy rows into SDL_GPU transfer buffers correctly.
- Gap found in Android EGL software upload:
  - `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Before the new patch, software upload copied every dirty row into `state.uploadScratch`, then uploaded the scratch buffer with tightly packed rows.
  - This adds one CPU memcpy pass for every dirty upload and hurts software path FPS.

Patch direction:

- Added `GetTextureRegionUploadPointer()` in `SDLAndroidFlutterPresenter.cpp`.
  - It validates RGBA, rect bounds, source pitch, row bytes, and `GL_UNPACK_ROW_LENGTH` availability when pitch is wider than dirty row bytes.
  - It returns a direct pointer to `texture->GetScanLineForRead(rect.y) + rect.x * 4` plus source pitch.
- Changed `EnsureAndroidEGLUploadTextureLocked()` to accept `pitch`.
  - On existing upload texture, calls `glTexSubImage2D` using direct source pointer and `GL_UNPACK_ROW_LENGTH` when needed.
  - On first/resize upload, allocates with `glTexImage2D(..., nullptr)` then uploads rect with `glTexSubImage2D`.
  - Restores `GL_UNPACK_ROW_LENGTH` to 0 after use; wider GL state restore still exists around presenter draw.
- Existing scratch-copy path remains as fallback when pitch/direct pointer is unavailable.

Expected effect:

- Software-upload EGL path avoids one row-copy pass for common CPU-backed RGBA textures.
- Dirty rect upload remains intact.
- This follows krkrsdl3's proven pitch-aware upload style and does not add hot-path graphical validation.

## 2026-07-04 continuation: Android EGL hot-path and duplicate upload reduction

User then asked to continue because FPS was still below expectation, especially while moving toward Flutter + SDL3 and away from Cocos. Three read-only subagents were used:

- Current-project path explorer:
  - Confirmed the current game frame path still enters through `tTVPBasicDrawDevice::Show()` and `TVPWindowLayer::UpdateDrawBuffer()`.
  - Confirmed `UpdateDrawBuffer()` already tries `TVPRuntimePresentHostWindowTexture()` first and returns on success, so Cocos sprite upload is mostly fallback for game frames when SDL runtime presenter fails.
  - Confirmed Cocos still owns the outer host/lifecycle and fallback path through `CocosRuntimeHost`, `TVPMainScene`, `DrawSprite`, and `GetAdapterTexture()`.
- Reference-project explorer:
  - Reconfirmed krkrsdl3-style pitch-aware upload with `GL_UNPACK_ROW_LENGTH`.
  - Reconfirmed AetherKiri-style separation between “frame dirty” and final `eglSwapBuffers`.
- SDL/runtime hot-path explorer:
  - Identified that Android takeover + GL-backed texture can still run SDL_GPU shadow upload before EGL native texture present.
  - That path can cause `TextureCache.Upsert()` to call `GetScanLineForRead()` on an OGL texture, which triggers `glReadPixels()` in `RenderManager_ogl.cpp`.
  - The same frame is then presented by Android EGL using the native GL texture, so the SDL_GPU shadow upload is redundant and expensive.

Local patch set after this continuation:

- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Added `skipShadowUploadForNativeAndroid`:
    - true only on Android when takeover is active and the texture has a native GL id.
    - Skips SDL_GPU shadow upload and shadow-upload failure logging for that native Android presenter case.
    - Keeps software textures and non-Android SDL_GPU shadow residency unchanged.
  - Rationale:
    - Android EGL native texture present is already CPU-copy-free.
    - Running SDL_GPU shadow upload first can force GPU->CPU readback for GL-backed textures and does not feed the EGL presenter.
    - This matches the reference-project preference for simple direct GL present instead of duplicating upload paths.
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Renamed restore helper to `RestoreAndroidEGLCurrentLocked(...)` and allowed an optional GL-state snapshot pointer.
  - Kept same-context GL-state restore when needed.
  - Moved per-frame `glFlush()` and `glGetError()` behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS`.
  - Normal path now goes straight from `glDrawArrays()` to `eglSwapBuffers()`.
  - Rationale:
    - `eglSwapBuffers()` submits the frame for the WindowSurface path.
    - Per-frame `glFlush()` + `glGetError()` is extra synchronization/driver work that reference projects generally avoid outside diagnostics/failure paths.
    - Failure logging for `eglSwapBuffers()` remains.
- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  - `PrepareTextureForExternalPresenter()` no longer calls `TVPSetRenderTarget(0)` unconditionally.
  - It detaches only when the OGL FBO is currently valid or the requested native texture is still the current render target.
  - Rationale:
    - Avoid redundant FBO binds on every native presenter frame.
    - Preserve the safety goal: external presenter must not sample a texture while it is still attached as the active render target.
- `cpp/core/environ/sdl/SDLGameManager.h`
  - `TVPSDLRecordBitmapCompletionEnd(...)` now returns `bool`.
- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - `TVPSDLRecordBitmapCompletionEnd(...)` returns true only when surface mirror is active, takeover is enabled, `TVPSDLPumpScreenPresenter("bitmap-end")` succeeds, and the batch was fully copied:
    - `regions > 0`
    - `surfaceCopied == regions`
    - `surfaceSkipped == 0`
    - `glBacked == 0`
    - `outOfBounds == 0`
  - Rationale:
    - Avoid marking the same completion dirty again when surface mirror already copied and presented every updated region.
    - Be conservative: mixed/incomplete/gl-backed/out-of-bounds batches still mark texture dirty as before.
- `cpp/core/visual/impl/BasicDrawDevice.cpp`
  - `EndBitmapCompletion()` now skips `texture->MarkDirtyRect(CompletionDirtyRect)` only when `TVPSDLRecordBitmapCompletionEnd()` reports a complete surface mirror present.
  - Rationale:
    - Avoid double CPU copy for surface-mirror completion updates.
    - Preserve correctness for default Android EGL path and for incomplete surface mirror batches.

Important nuance:

- The Android EGL presenter still uses the current EGL context from the presenter thread. The patch does not introduce a separate EGL context.
- Because the same context may be shared with the engine/Cocos host, GL state protection is still kept when the previous context equals the presenter context.
- The main default-path win in `SDLAndroidFlutterPresenter.cpp` is removing `glFlush()` and `glGetError()` from every normal frame, not removing all GL state restore.

Verification after this continuation:

- `git diff --check` passed.
- No local C++ build was run because the environment has no local `clang-format` and prior build-tool checks found native build tools unavailable.
- CI/GitHub Actions should be used for compile verification after commit/push.

Expected runtime impact:

- Android OpenGL/nativeGL takeover should no longer pay the SDL_GPU shadow-upload/readback cost before EGL native present.
- Native EGL present should do less per-frame synchronization work by default.
- Surface-mirror text/image completion, when explicitly enabled and fully copied, should avoid a second texture dirty/present pass for the same regions.

If logs are needed:

- Use `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS=1`.
- Without that switch, success samples like `present-android-egl` are intentionally quiet by default.
- With the switch, compare:
  - whether `present-texture-egl` still reports full dirty on small text updates.
  - whether `nativeGL=1 cpuCopyFree=1` stays on the fast path.
  - whether surface mirror batches show complete `surfaceCopied == regions` when that path is intentionally enabled.

Commit/push state after this continuation:

- Committed and pushed as `10341a3 Reduce Android native present overhead`.
- GitHub Actions for head
  `10341a3967d92f6905bcaa050d9d297aee926cfa` completed successfully:
  - `Build Flutter Android`: success.
  - `Code Format Check`: success.
