# 2026-07-01 Session Continuation Memory

This file records the current session state for future context recovery.
Only `/root/kiriki-work/KiriKiri-LauncherC` was modified. Reference projects
remain read-only references unless explicitly stated otherwise.

## Hard Goals From User

- Continue migrating the project toward **Flutter + SDL3**.
- Gradually remove Cocos from the runtime/render host path.
- Keep implementation complete, high-performance, and highly compatible.
- Prefer proven designs from:
  - `/root/kiriki-work/AetherKiri`
  - `/root/kiriki-work/docs`
  - `/root/kiriki-work/kirikiroid2-web`
  - `/root/kiriki-work/KrKr2-Next`
  - `/root/kiriki-work/krkrsdl2-main`
  - `/root/kiriki-work/krkrsdl3-main`
  - `/root/kiriki-work/SDL-release-3.4.10`
- Avoid excessive validation in hot render paths. Boundary/lifecycle guards are
  acceptable; per-frame rendering should stay lean.
- Keep detailed memory docs because context may be compacted.

## Current Repository State

Repository:

- `/root/kiriki-work/KiriKiri-LauncherC`

Branch:

- `main`

Remote:

- `origin https://github.com/xiaocongyu66/KiriKiri-LauncherC.git`

Current sync state at memory creation:

- `HEAD -> main`
- `origin/main`
- latest commit: `4b410db Move Android EGL presentation into Flutter presenter`
- `git status --short --branch` showed `## main...origin/main`
- Worktree was clean after push.

Recent pushed commits in this session:

1. `9fbcfe3 Stabilize FreeType text metrics and split Flutter presenter`
2. `4b410db Move Android EGL presentation into Flutter presenter`

Push notes:

- Push used interactive HTTPS credential entry.
- Do not write the GitHub token into repository files, remotes, git config, or
  memory files.

## CI State At Interruption

GitHub Actions after pushing `4b410db`:

- `Code Format Check`
  - run id: `28477088714`
  - commit: `4b410db`
  - status: completed
  - conclusion: success
  - URL:
    `https://github.com/xiaocongyu66/KiriKiri-LauncherC/actions/runs/28477088714`
- `Build Flutter Android`
  - run id: `28477132296`
  - commit: `4b410db`
  - status: in_progress at last poll
  - URL:
    `https://github.com/xiaocongyu66/KiriKiri-LauncherC/actions/runs/28477132296`

Local Android build remains blocked:

```sh
/root/kiriki-work/KiriKiri-LauncherC/platforms/android/gradlew \
  -p /root/kiriki-work/KiriKiri-LauncherC/platforms/android assembleRelease
```

Result:

- `JAVA_HOME` is unset.
- no `java` command is available in `PATH`.
- Therefore local Gradle build cannot verify Android compilation in this
  container. Use GitHub Actions for real Android build feedback unless a local
  JDK/SDK/NDK is installed.

## Commit 9fbcfe3: FreeType Crash Fix + Initial Presenter Split

Reason:

- Latest `/root/log/78.log` showed repeated native crashes:
  - `Fatal signal 11 (SIGSEGV), fault addr 0x1a`
  - thread: `GLThread`
  - top frame: `FreeTypeFontRasterizer::GetAscentHeight()+24`
  - called through:
    - `tTVPNativeBaseBitmap::ApplyFont`
    - `GetTextSize` / `GetTextHeight` / `GetTextWidth`
    - `tTJSNI_BaseLayer`
    - TJS script chain
- This was backlog/text-measurement related, not EGL/Flutter presenter related.

Main FreeType changes:

- `cpp/core/visual/FreeType.cpp`
  - Added `FreeTypeRefCount`.
  - `TVPInitializeFont()` now checks `FT_Init_FreeType` result.
  - `tFreeTypeFace` constructor initializes `FTFace = nullptr`.
  - Constructor throws if `Face->GetFTFace()` is null.
  - Constructor catch path deletes partial state and decrements FreeType refcount.
  - `tFreeTypeFace::~tFreeTypeFace()` now pairs the FreeType library release
    with face ownership.
  - `SetHeight()` clamps non-positive height to `1` and handles null `FTFace`.
  - `LoadGlyphSlotFromCharcode()` returns false when `FTFace` is null.
- `cpp/core/visual/FreeType.h`
  - `GetDefaultChar()`, `GetFirstChar()`, `GetAscent()`, `GetUnderline()`, and
    `GetStrikeOut()` handle null/invalid FreeType internals.
- `cpp/core/visual/FreeTypeFontRasterizer.cpp`
  - Removed rasterizer-level `TVPUninitializeFreeFont()` ownership.
  - `ApplyFont()` now creates and sizes a new face before replacing the old one.
  - Clears fallback face when primary face changes.
  - `GetTextExtent()` initializes `w/h`.
  - `GetBitmap()` and `GetGlyphDrawRect()` return safely without a current face.

Why this matches the performance requirement:

- These guards are on font lifecycle and text metrics failure paths, not inside
  the hot render present path.

Initial presenter split in the same commit:

- Added:
  - `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.h`
  - `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- Moved direct Flutter `ANativeWindow_lock` CPU fallback presentation and
  surface-size state out of `SDLGameManager.cpp`.
- CMake updated to compile `SDLAndroidFlutterPresenter.cpp`.
- Behavior preserved:
  - EGL path first.
  - direct Flutter CPU fallback second.
  - dirty rect ownership and consumption remain in `TVPSDLTryPresentTexture()`.
  - partial direct present remains opt-in through
    `KRKR2_ANDROID_DIRECT_PARTIAL_PRESENT=1`.

## Commit 4b410db: Android EGL Presenter Moved Into Presenter Module

Goal:

- Continue new architecture migration by making Flutter/Android presentation a
  dedicated module.
- Reduce Cocos-era `SDLGameManager.cpp` ownership of low-level Android/EGL
  presentation.

Moved from `SDLGameManager.cpp` into `SDLAndroidFlutterPresenter.cpp`:

- `TVPAndroidEGLSurfacePresenterState`
- EGL presenter mutex and counters
- EGL enable/flip/software-upload flags
- GL state snapshot/restore helpers
- EGL error formatting
- shader/program setup
- EGL config selection
- EGL window surface create/drop/reset
- software texture upload path
- `TryPresentAndroidEGLSurfaceTexture`
- presenter plan execution equivalent to old `TryPresentAndroidTexturePlan`
- EGL overlay/diagnostics state
- EGL surface-change reset handling

New/expanded public API in `SDLAndroidFlutterPresenter.h`:

- `TVPSDLAndroidFlutterPresenterTryPresentTexturePlan(...)`
- `TVPSDLAndroidFlutterPresenterPresentPathLogName(...)`
- `TVPSDLAndroidFlutterPresenterAppendEGLOverlayInfo(...)`
- `TVPSDLAndroidFlutterPresenterIsEGLHighPerformanceActive()`
- `TVPSDLAndroidFlutterPresenterNotifySurfaceChanged(...)`

What remains in `SDLGameManager.cpp` intentionally:

- `TVPSDLTryPresentTexture()` still owns:
  - takeover/shadow-upload policy
  - dirty rect collection and clamping
  - force full-frame after surface change
  - runtime frame accounting
  - `texture->ConsumeDirtyRect(...)`
  - high-level present logs
- SDL GPU shadow-upload state remains there.
- Android input/touch coordinate mapping remains there.
- C ABI wrapper `TVPSDLNotifyAndroidFlutterGameSurfaceChanged(const char *)`
  remains there for Android JNI compatibility and delegates to presenter API.
- `TVPAndroidGetFlutterGameSurfaceSize(...)` is still used by input clamping
  fallback; this should move behind presenter API in a later slice.

Checks before pushing `4b410db`:

```sh
git -C /root/kiriki-work/KiriKiri-LauncherC diff --check
python3 -m json.tool /root/kiriki-work/KiriKiri-LauncherC/vcpkg.json >/dev/null
```

Both passed.

## 2026-07-02 Latest 78 Game Render Regression: no-native-gl Fallback

User-provided logs:

- `/root/log/20260702073424068.log`
- `/root/log/20260702073513622.log`
- `/root/log/20260702073559297.log`
- `/root/log/20260702073709998.log`
- `/root/log/78.log`

User report:

- This game's OpenGL render pipeline feels worse than software rendering.
- Continue Flutter + SDL3 migration, remove Cocos gradually.
- Keep implementation complete, high performance, high compatibility.
- Avoid heavy validation in render hot paths.

Important log finding:

- The early runs use the Android EGL presenter fast path:
  `present-android-egl ... nativeGL=5 softwareUpload=0`.
- The later/problematic run falls out of EGL because the final present texture
  has no native GL id:
  `android-egl-presenter unavailable ... reason=no-native-gl surface=1920x1080`.
- Immediately after that, the presenter falls back to the direct Flutter CPU
  path:
  `present-flutter-direct ... glBacked=0`
  and
  `present-texture-direct ... fullFrame=1`.
- For 1920x1080 RGBA this direct path writes about 8.3 MiB per full frame via
  `ANativeWindow_lock`/memcpy. At high frame rates this is hundreds of MiB/s of
  CPU memory traffic and can easily feel worse than the software renderer.

Root cause:

- `iTVPTexture2D::GetNativeGLTextureId()` returns `0` for software textures.
- OpenGL textures override it and normally return a GL texture id.
- `SDLAndroidFlutterPresenter.cpp::TryPresentAndroidEGLSurfaceTexture()` had:

```cpp
const GLuint nativeTexture = texture->GetNativeGLTextureId();
const bool softwareUpload =
    nativeTexture == 0 && IsAndroidEGLSoftwareUploadEnabled();
if(nativeTexture == 0 && !softwareUpload) {
    ... reason=no-native-gl ...
    return false;
}
```

- `KRKR2_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE` existed but was default-off.
- Therefore a non-GL-backed final texture forced EGL presenter failure and fell
  into the slower direct Flutter CPU presenter.

Reference-project agreement:

- AetherKiri and krkrsdl3 both keep a persistent GL texture for upload-backed
  presentation.
- They use `glTexSubImage2D` when the texture size is unchanged and only use
  `glTexImage2D` when the size changes.
- Their upload paths prefer pitch-aware GL upload via `GL_UNPACK_ROW_LENGTH`
  where supported, with compact-row fallback where needed.
- Presenter should remain a final-output stage, not mix in KiriKiri layer logic.

Patch applied in this session:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Added `uploadSourceTexture` to the EGL presenter state so the presenter can
    know whether the persistent software-upload texture already represents the
    same source texture.
  - Changed EGL software upload fallback to default-on for Android.
    - New explicit opt-out: `KRKR2_DISABLE_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE`.
    - Existing `KRKR2_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE=0/1` is still honored
      if set, for compatibility/diagnostics.
  - Replaced full-frame-only CPU scratch copy with
    `CopyTextureRegionToAndroidEGLScratch(...)`.
    - First frame, source texture change, or size change copies the full frame.
    - Subsequent frames copy only the dirty rect.
  - Changed `EnsureAndroidEGLUploadTextureLocked(...)` to upload a rect:
    - same dimensions: `glTexSubImage2D(... rect.x, rect.y, rect.w, rect.h ...)`;
    - size change: `glTexImage2D(...)` with a full-frame scratch copy.
  - Log now includes both the logical dirty rect and actual upload rect:
    `rect=... upload=... nativeGL=... softwareUpload=...`.
  - Added `nativePresents` counter so overlay/diagnostics can distinguish
    native GL zero-copy-ish presents from EGL software-upload presents.
  - Added `TVPSDLAndroidFlutterPresenterIsEGLCpuCopyFreeActive()`.
  - `TVPSDLAndroidFlutterPresenterIsEGLCpuCopyFreeActive()` reports whether
    the last successful EGL present used a native GL texture, not whether every
    EGL present since process start was native. This keeps diagnostics useful if
    a game temporarily falls back to software upload and later returns to native
    GL.

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.h`
  - Declared `TVPSDLAndroidFlutterPresenterIsEGLCpuCopyFreeActive()`.

- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - Overlay still marks EGL presenter as high-performance when EGL is active.
  - Overlay now marks `cpuCopyFreePresenter` only when all EGL presents so far
    were native GL presents.
  - `present-texture-egl/direct` log now includes:
    `nativeGL=<0/1> cpuCopyFree=<0/1>`.

Expected behavior after this patch:

- If the final texture is native OpenGL-backed, behavior remains the fast path:
  EGL samples the native texture after
  `PrepareTextureForExternalPresenter(texture)`.
- If the final texture is software-backed, EGL no longer immediately returns
  `reason=no-native-gl` by default. It uploads the software pixels into a
  persistent GL texture and presents through the Android EGL surface.
- This is not true zero-copy, but it avoids the worst fallback:
  `ANativeWindow_lock` full-frame CPU writes to the Flutter surface.
- Logs should show:
  `present-android-egl ... nativeGL=0 softwareUpload=1 upload=...`
  instead of:
  `android-egl-presenter unavailable ... reason=no-native-gl`
  followed by `present-flutter-direct ... glBacked=0`.

Renderer preference note:

- Android defaults in `PreferenceDefaults.cpp` set `renderer=opengl`, but only
  when the preference key is missing.
- Old per-game/global preference files may still contain `renderer=software`.
- If `renderer=software` is active, final textures can legitimately be
  `glBacked=0`; do not expect native GL zero-copy in that mode.
- Future diagnostics should make the actual render manager/pref source easier
  to see in logs.

Useful follow-up:

- Add a render-manager API such as
  `PrepareNativeGLTextureForExternalPresenter(texture, rect)` so presenter does
  not directly depend on `GetNativeGLTextureId()` semantics, especially for OGL
  split textures.
- Add a real pitch-aware upload helper for EGL software upload:
  pass scanline pointer + pitch + rect into GL upload, use
  `GL_UNPACK_ROW_LENGTH` when available, and compact rows only when required.
- Continue migrating Cocos host out of the final present path. Logs still show
  `runtimeHost=cocos2d`, so the Flutter + SDL3 architecture is not complete.

Local Gradle build was attempted and blocked by missing Java as described
above.

## Important Current Files

Primary changed files:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.h`
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`
- `cpp/core/visual/FreeType.cpp`
- `cpp/core/visual/FreeType.h`
- `cpp/core/visual/FreeTypeFontRasterizer.cpp`
- `cpp/core/environ/CMakeLists.txt`
- `docs/runtime-host-architecture.md`
- `memory/2026-07-01-temp-krkrsdl2-3-notes.md`

Detailed companion memory already updated:

- `memory/2026-07-01-temp-krkrsdl2-3-notes.md`

## Known Risks And Next Checks

Highest priority after resuming:

1. Poll GitHub Actions run `28477132296`.
2. If it fails, inspect logs and fix compile/runtime issues.
3. If it passes, continue migration.

Likely compile risk areas from the large EGL presenter move:

- Missing include/linkage around Android EGL/GLES symbols in
  `SDLAndroidFlutterPresenter.cpp`.
- Hidden dependencies that used to live in `SDLGameManager.cpp` anonymous
  namespace.
- `TVPSetRenderTarget(GLuint)` declaration now lives in presenter cpp under
  Android/GLES includes.
- Non-Android builds must not see bare `EGL*`, `GLuint`, `ANativeWindow`, or
  `ARect` types outside `#if defined(__ANDROID__)`.

Static checks already suggest these were handled, but CI is authoritative.

## Next Migration Candidates

Short-term candidates:

1. Add presenter API for Flutter surface size so `SDLGameManager.cpp` no longer
   declares `TVPAndroidGetFlutterGameSurfaceSize(...)`.
2. Split SDL GPU shadow upload out of `SDLGameManager.cpp` into its own
   presenter-adjacent module.
3. Extract Android Cocos input fallback from `platforms/android/cpp/krkr2_android.cpp`
   into a Cocos adapter module.
4. Add an `SDLRuntimeHost` skeleton behind a flag, keeping current Cocos host as
   default until event pumping and metrics are proven.

Rendering correctness/performance candidates from prior analysis:

- Investigate `RenderManager_ogl.cpp` state/cache issues against AetherKiri and
  krkrsdl3:
  - pixel format mapping, especially BGRA/RGBA behavior
  - `GL_UNPACK_ROW_LENGTH` / `GL_UNPACK_ALIGNMENT`
  - FBO binding cache invalidation
  - removing Cocos texture/shader adapter dependence
- The previous visual symptom was OpenGL path rendering the top layer of a rich
  background image gray/white while a shifted lower layer was colored. That is
  likely not the Android EGL presenter itself; it may be GL texture format,
  FBO/cache, or Cocos adapter state.

## Reference Notes

Reference projects are useful but should not be modified:

- AetherKiri:
  - has stronger render manager decoupling from Cocos.
  - has useful GL texture wrapper and pixel format mapping.
- krkrsdl3-main:
  - useful SDL3 callback lifecycle and GL sprite presenter model.
  - useful pitch-aware GL upload with `GL_UNPACK_ROW_LENGTH`.
- krkrsdl2-main:
  - useful dirty rect union and software surface-to-texture model.

## Operational Notes

- `rg` is unavailable in this environment. Use `grep`, `find`, `awk`, `sed`.
- Local Java/Android build is unavailable until JDK/SDK/NDK are installed.
- GitHub Actions should be used for Android build verification after push.
- Do not store GitHub credentials in files.

## 2026-07-02 Continuation

User supplied updated logs:

- `/root/log/78.log`
- `/root/log/20260702040932971.log`

Runtime log findings:

- No repeated FreeType `GetAscentHeight` crash was visible in the new tail.
- Android EGL presenter was active and presenting:
  - `present-android-egl #1/#2/#.../#512`
  - `nativeGL=5`
  - `softwareUpload=0`
  - `uv=0.9375,0.5273`
  - `flipY=1`
- This confirms the high-performance EGL path is in use on the device logs.
- `/root/log/78.log` still contained a Cocos GL error:
  - `OpenGL error 0x0502 ... CCTexture2D.cpp initWithMipmaps 665`
  - This supports continuing the migration away from the Cocos render host.

CI investigation:

- GitHub Actions job:
  - workflow: `Build Flutter Android`
  - run id: `28477132296`
  - job id: `84404443824`
  - conclusion: failure
- Plain unauthenticated job log download returned `403`.
- A token was read only from the old Codex session for the temporary `curl`
  command; it was not written to files, git config, remotes, or memory.
- Downloaded log to:
  - `/tmp/kiriki-build-28477132296-auth.log`

CI failure root cause:

- The Android build failed compiling:
  - `cpp/core/environ/sdl/SDLGameManager.cpp`
- Errors included:
  - `use of undeclared identifier 'IsSDLScreenPresenterWindowSupported'`
  - `use of undeclared identifier 'ShouldUseSDLSurfaceMirrorForTakeover'`
  - `use of undeclared identifier 'DestroySDLScreenPresenterLocked'`
  - `use of undeclared identifier 'SDLScreenPresenterUnsupportedReason'`
  - `use of undeclared identifier 'DropSDLSurfaceMirror'`
  - `use of undeclared identifier 'IsSDLRenderDiagnosticsActive'`
  - `use of undeclared identifier 'ShouldRunSDLGpuShadowUpload'`
  - `use of undeclared identifier 'CopyRegionToSDLSurfaceMirror'`
  - ambiguous call to `TVPSDLTryPresentTexture`
  - final `expected '}'`

Cause:

- During the previous EGL presenter extraction, a large non-EGL helper block in
  `SDLGameManager.cpp` was accidentally removed, and the anonymous namespace
  boundary was lost.
- The missing block contained the SDL screen presenter helpers, SDL surface
  mirror helpers, runtime initialization functions, input queue functions,
  bitmap completion diagnostics, and loading console functions.
- This was a compile-structure regression, not a runtime EGL design problem.

Fix applied locally:

- Restored the missing non-EGL helper/API block from commit `9fbcfe3` into
  `cpp/core/environ/sdl/SDLGameManager.cpp`.
- Kept Android EGL-specific implementation in
  `SDLAndroidFlutterPresenter.cpp`; EGL code was not moved back.
- Restored anonymous namespace closure before the public API implementations,
  matching the previous file structure:
  - internal helpers stay in anonymous namespace.
  - `TVPSDLInitializeRuntime`, `TVPSDLGetRuntimeInfo`,
    `TVPSDLRecordRenderOverlayFrame`, `TVPSDLTryPresentTexture`,
    `TVPSDLPumpScreenPresenter`, and `TVPSDLRunGameLaunch` are global API
    definitions.

Checks after the fix:

```sh
git -C /root/kiriki-work/KiriKiri-LauncherC diff --check
python3 -m json.tool /root/kiriki-work/KiriKiri-LauncherC/vcpkg.json >/dev/null
```

Both passed.

Local Android Gradle build is still unavailable in this environment because no
local Java runtime is installed. Use GitHub Actions after push for final Android
compile verification.

Reference-project notes from read-only sub-agent:

- `krkrsdl3-main/cpp/krkrsdl_gl.cpp`
  - Uses simple RGBA upload.
  - Sets `GL_UNPACK_ROW_LENGTH = pitch / 4` and `GL_UNPACK_ALIGNMENT = 1`.
  - Resets row length/alignment after upload.
  - Good for pitch-aware upload strategy, but it does not save enough GL state
    for Flutter external surface rendering.
- `krkrsdl3-main/cpp/core/render/RenderManagerGL.cpp`
  - Uses `GL_UNPACK_ROW_LENGTH` for non-tight pitch.
  - Has FBO helpers but does not fully restore viewport/scissor/blend in all
    paths.
- `AetherKiri/cpp/core/environ/stubs/ui_stubs.cpp`
  - Host blit first prepares source texture, then binds host render target.
  - Native GL texture fast path calls `TVPSetRenderTarget(0)` before sampling.
  - CPU fallback reuses texture with `glTexSubImage2D` when size is unchanged.
- `AetherKiri/bridge/engine_api/src/engine_api.cpp`
  - Exposes host frames as RGBA8888.
  - Reads complete render target rather than relying on current viewport.
- `KiriKiri-LauncherC/cpp/core/visual/ogl/RenderManager_ogl.cpp`
  - `InternalUpdate` remains the best local model for robust pitch handling:
    use `GL_EXT_unpack_subimage` / `GL_UNPACK_ROW_LENGTH` when available;
    otherwise repack into a tight temporary buffer.
  - BGRA input is either uploaded via `GL_BGRA_EXT` or converted to RGBA.

Next technical target:

- Keep current Android EGL presenter state snapshot strategy.
- Add `GL_UNPACK_ROW_LENGTH` to the Android EGL presenter GL state snapshot
  before introducing row-length uploads in presenter code.
- Extract a reusable pitch-aware OpenGL upload helper from
  `RenderManager_ogl.cpp::InternalUpdate` style logic.
- Keep Flutter/Android presenter main path RGBA8888; handle BGRA at texture
  loading/upload compatibility boundaries.

## 2026-07-02 Follow-up Migration Slice

After pushing `0ed019f Restore SDL presenter helpers after EGL split`, the
next user request was to continue the Flutter + SDL3 migration by:

- explicitly linking `SDL3::SDL3` and Android graphics/system libraries on the
  top-level Android `krkr2` target.
- moving the presenter-side `TVPSetRenderTarget(GLuint)` dependency behind a
  render-manager interface so `SDLAndroidFlutterPresenter.cpp` no longer has a
  bare GL extern.

Sub-agent findings used for this slice:

- The final Android shared library target is created in root `CMakeLists.txt`
  as `krkr2`.
- The build currently gets Android system libraries mostly by transitive links:
  `krkr2 -> krkr2plugin -> krkr2core -> Android libs`.
- That works today but is fragile as the plugin/core split changes.
- The low-risk hardening is to keep existing transitive links and additionally
  link the top-level Android target directly to:
  - `SDL3::SDL3`
  - `log`
  - `android`
  - `EGL`
  - `GLESv2`
  - `GLESv3`
  - `GLESv1_CM`
  - `vulkan`
  - `OpenSLES`
- `TVPSetRenderTarget(GLuint)` is an OpenGL render-manager internal FBO/cache
  operation. It should not be a presenter-level link contract.
- The public cross-module interface should stay GL-free and use
  `iTVPTexture2D *`.

Local changes applied:

- `CMakeLists.txt`
  - In the Android branch after `add_library(${PROJECT_NAME} SHARED ...)`,
    added `find_package(SDL3 CONFIG REQUIRED)`.
  - Linked the top-level `krkr2` target directly against SDL3 and Android
    graphics/system libraries listed above.
- `cpp/core/visual/RenderManager.h`
  - Added virtual no-op:
    `PrepareTextureForExternalPresenter(iTVPTexture2D *texture)`.
  - The default implementation is GL-free and only consumes the parameter.
- `cpp/core/visual/ogl/RenderManager_ogl.cpp`
  - Made file-local `TVPSetRenderTarget(GLuint)` `static`.
  - Added `TVPRenderManager_OpenGL::PrepareTextureForExternalPresenter(...)`
    override that calls `TVPSetRenderTarget(0)`.
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Removed `extern void TVPSetRenderTarget(GLuint target);`.
  - Replaced direct call with:
    `TVPGetRenderManager()->PrepareTextureForExternalPresenter(texture);`
  - Presenter no longer exposes or links against that GL helper symbol.

Checks after this slice:

```sh
git -C /root/kiriki-work/KiriKiri-LauncherC diff --check
python3 -m json.tool /root/kiriki-work/KiriKiri-LauncherC/vcpkg.json >/dev/null
grep -RIn "extern void TVPSetRenderTarget\\|TVPSetRenderTarget" \
  /root/kiriki-work/KiriKiri-LauncherC/cpp \
  /root/kiriki-work/KiriKiri-LauncherC/platforms --exclude-dir=.git
```

Results:

- `diff --check` passed.
- `vcpkg.json` parse passed.
- No `extern void TVPSetRenderTarget` remains.
- `TVPSetRenderTarget` only remains as a `static` helper and same-file calls in
  `RenderManager_ogl.cpp`.

CI state while applying this slice:

- `0ed019f` Android build run:
  - run id: `28546701023`
  - status at last poll: `in_progress`
  - URL:
    `https://github.com/xiaocongyu66/KiriKiri-LauncherC/actions/runs/28546701023`

## 2026-07-02 EGL Pixel-Store Snapshot Prep

After the explicit-link/interface slice, a small Android EGL presenter prep
change was made for future pitch-aware uploads.

Reason:

- Reference analysis showed krkrsdl3 and AetherKiri both rely on
  `GL_UNPACK_ROW_LENGTH` for pitched uploads.
- The Android Flutter EGL presenter already saves/restores important GL state
  around external surface presentation.
- Before adding row-length uploads, that state snapshot must also preserve
  `GL_UNPACK_ROW_LENGTH` when the current GL context supports it.

Change:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
  - Added optional `unpackRowLength` field to `TVPAndroidGLStateSnapshot`.
  - Added token-aware extension helper for `GL_EXT_unpack_subimage`.
  - Added one-time runtime support detection:
    - true for OpenGL ES 3.x / 4.x contexts.
    - true for GLES2 contexts advertising `GL_EXT_unpack_subimage`.
  - `SaveAndroidGLState()` now reads `GL_UNPACK_ROW_LENGTH` only when supported.
  - `RestoreAndroidGLState()` restores `GL_UNPACK_ROW_LENGTH` only when
    supported.

Compatibility note:

- The code is guarded by `#if defined(GL_UNPACK_ROW_LENGTH)`.
- It also checks runtime support before touching the enum. This avoids
  `GL_INVALID_ENUM` on GLES2 drivers that do not support row-length pixel
  storage.

Checks:

```sh
git -C /root/kiriki-work/KiriKiri-LauncherC diff --check
python3 -m json.tool /root/kiriki-work/KiriKiri-LauncherC/vcpkg.json >/dev/null
```

Both passed.
