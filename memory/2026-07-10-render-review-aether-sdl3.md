# 2026-07-10 Render Review / AetherKiri + SDL3 Migration Memory

## Scope

- Writable project: `/root/kiriki-work/KiriKiri-LauncherC` only.
- Reference-only projects: `/root/kiriki-work/AetherKiri`, `KrKr2-Next`, `krkrsdl2-main`, `krkrsdl3-main`, `SDL-release-3.4.10`, `docs`.
- Hard target remains Flutter + SDL3 and gradual Cocos removal.
- Hard render contract remains fixed game surface `1920x1080`. Do not change the game image to physical screen size or dynamic viewport size.

## Reference Conclusions

- AetherKiri model: render/blit one complete frame into the Android EGL window surface, mark that surface dirty, and call `eglSwapBuffers()` only when a new frame was actually produced. This avoids swapping stale double-buffer contents.
- krkrsdl3 model: deterministic full-frame present in the SDL app iteration. One app iteration owns event handling, engine frame execution, draw, and swap.
- LauncherC is currently a hybrid: native OpenGL texture is blitted into a Flutter `ANativeWindow`, marked dirty, then swapped later from the screen presenter pump. This is acceptable only if all present metadata and GL state are kept coherent.
- Remaining Cocos blockers are not only rendering. Cocos still owns desktop process lifetime, some launch fallback UI, and frame cadence through `TVPMainScene::update()`.

## Logs / Artifact Diagnosis

- Latest useful render log: `/root/log/20260707044143211.log`.
- OpenGL/EGL path reports fixed texture/output: `texture=1920x1080 output=1920x1080 viewport=0,0,1920x1080 fullFrame=1`.
- Native texture path reports `nativeGL=5 softwareUpload=0 uv=0.9375,0.5273 flipY=0`. This means the visible `1920x1080` frame is sampled from a larger likely `2048x2048` backing texture. If the source viewport/origin is wrong, stale/cropped side content can appear even while the software path is correct.
- Older crash logs `/root/log/20260707044135183.log` and `/root/log/20260707044141282.log` show `UnsatisfiedLinkError` for `KR2Activity.setUseFFmpegImageDecoder`. Current `SdlRuntimeActivity.kt` already moved runtime init before that call, so those logs likely came from a pre-fix build.

## Fixes Applied This Turn

- `SDLAndroidFlutterPresenter.cpp`
  - Made Flutter surface bridge JNI hooks weak (`TVPAndroidAcquireFlutterGameSurfaceWindow`, `TVPAndroidReleaseFlutterGameSurfaceWindow`, `TVPAndroidGetFlutterGameSurfaceSize`) and added null checks before use. This prevents Android variants without the Flutter surface bridge from hard link-failing.
  - Moved EGL blit program creation until after the presenter has switched to its EGL surface/context and after the previous same-context GL state snapshot is captured. This fixes a first-use GL state leak where `EnsureAndroidEGLProgramLocked()` could clobber `GL_ARRAY_BUFFER` before it was saved.
  - Changed GL state snapshot attr capture to save fixed attrib indices `0` and `1` before program creation. The presenter binds `aPosition` and `aTexCoord` to these locations, so this is stable and avoids needing the program to exist before save.
  - If restoring the previous EGL context/state fails after blitting, the presenter now logs and returns failure before marking the frame dirty. A failed restore should not be recorded as a valid queued/presented frame.

## Existing Important Changes Still Present

- `SDLPresentTypes.h` asserts fixed `1920x1080` via `kTVPSDLFixedGameSurfaceWidth/Height`.
- `LayerIntf.cpp` GPU transparent/transition child composition matches AetherKiri more closely: full parent temp texture, full child composition rect, `CopySelfForRect()` always initializes the temp, and final GPU window completion is full frame.
- `SDLAndroidFlutterPresenter.cpp` uses `uUvRect` groundwork instead of a simple scale uniform. Current rect remains `0,0,uvScaleU,uvScaleV`; if artifacts persist, next likely test is deriving a source origin from render-manager/FBO semantics instead of assuming visible pixels start at UV `(0,0)`.
- Android EGL present remains deferred: producer queues/marks dirty, pump/frame-end drains via `TVPSDLAndroidFlutterPresenterSwapIfDirty()`.

## Review Findings To Keep

- Performance risk: GL state preservation calls many `glGet*`/`glIsEnabled` queries. It is safer while sharing context with the engine, but expensive. Long-term fix is to move presenter ownership into a cleaner SDL3-owned GL context or a render-manager-owned external presenter path, not keep adding checks in the hot path.
- Artifact risk: native GL zero-copy is currently the suspect path, because software rendering produces correct images. Logs show sampling from a padded internal texture (`uv=0.9375,0.5273`). Check UV origin/flip and FBO viewport semantics before changing unrelated layer logic.
- Fixed surface risk: logs also show Android/launcher UI physical viewport `2780x1264` and scene `2048x931`, but user requirement is still fixed game image `1920x1080`. The correct fix is letterbox/host UI composition around a fixed 16:9 game surface, not resizing the game surface.
- Build risk: `SdlRuntimeActivity.kt` imports Flutter embedding classes, but current Android Gradle files do not visibly include Flutter embedding/module wiring. This should be addressed before making SDL runtime the only path.
- SDL Java risk: `SdlRuntimeActivity` is a plain `Activity`, while SDL Android Java often assumes `SDLActivity` static state/`SDLSurface`. The new runtime needs either a compatible SDL Java host setup or a fully native/bridge-owned surface model that avoids those assumptions.

## Next Concrete Steps

1. Inspect/update Android Gradle Flutter embedding integration so `SdlRuntimeActivity.kt` compiles without hidden external setup.
2. Add an SDL runtime host independent of Cocos and move frame ownership toward krkrsdl3-style `SDL_AppIterate()` / Android frame pump.
3. Keep native EGL full-frame dirty/swap path, but investigate UV origin for padded OpenGL textures. Compare engine FBO coordinate convention with software readback and AetherKiri `UpdateDrawBuffer`.
4. Only after SDL runtime, events, presenter, and launch fallback are stable, flip Android defaults away from Cocos and then remove Cocos dependencies.

## Validation

- `git diff --check` passed after this turn's edits.
- Full Android build was not run locally because this environment previously lacked Android SDK configuration (`ANDROID_HOME` / `local.properties sdk.dir`).
