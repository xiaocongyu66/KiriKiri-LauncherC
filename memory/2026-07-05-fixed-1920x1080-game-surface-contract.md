# 2026-07-05 fixed 1920x1080 game surface contract

## Scope

Only modify:

- `/root/kiriki-work/KiriKiri-LauncherC`

Reference only:

- `/root/kiriki-work/AetherKiri`
- `/root/kiriki-work/docs`
- `/root/kiriki-work/kirikiroid2-web`
- `/root/kiriki-work/KrKr2-Next`
- `/root/kiriki-work/krkrsdl2-main`
- `/root/kiriki-work/krkrsdl3-main`
- `/root/kiriki-work/SDL-release-3.4.10`

Hard target:

- Migrate to Flutter + SDL3.
- Remove Cocos2dx from the final game render path.
- Keep the renderer complete, high performance, and high compatibility.
- Copy whole proven render-chain behavior from AetherKiri / krkrsdl3 rather
  than assembling unrelated fragments.

## User correction that must not be missed

The user clarified that "fixed resolution is normal 1920x1080" and then
clarified again:

`我指的游戏画面必须得是1920 1080的`

This overrides the previous full-surface presenter note. The native game output
buffer is not allowed to follow the Flutter overlay size, device size, DPR, or
physical screen size.

Current non-negotiable contract:

1. The game/native external presenter target is fixed `1920x1080`.
2. Flutter creates the game `SurfaceTexture` with default buffer size
   `1920x1080`.
3. Android native `ANativeWindow_setBuffersGeometry()` uses `1920x1080`.
4. SDL/Android EGL presenter output size is `1920x1080`.
5. Direct `ANativeWindow_lock/unlockAndPost` fallback output size is also
   `1920x1080`.
6. Flutter may visually scale the `Texture` with contain/center layout inside
   the overlay, but this is only display scaling. It is not the game buffer.
7. Flutter input maps from the displayed contained rectangle back to fixed
   `1920x1080` coordinates.
8. Black bars outside the contained game rectangle should not drive game input.

This means there are two different concepts:

- fixed native buffer: always `1920x1080`;
- visible Flutter rectangle: whatever logical size fits `16:9` in the overlay.

Do not collapse them again.

## Files changed in this correction

### Flutter UI / input

File:

- `flutter_launcher/lib/src/pages/game_overlay_page.dart`

Important rules now encoded:

- `_gameBufferWidth = 1920`
- `_gameBufferHeight = 1080`
- `_gameAspectRatio = 16:9`
- `_scheduleGameSurface()` has no external width/height parameters and always
  requests the fixed buffer.
- `_ensureGameSurface()` calls Android host channel methods with
  `width=1920,height=1080`.
- `_refreshGameSurfaceMetrics()` no longer treats native content/presented
  viewport metrics as a resize authority. It only synchronizes state when
  native reports the fixed surface size.
- `_toSurfacePosition()` maps local pointer coordinates inside the displayed
  contained game widget directly to fixed `1920x1080`.
- `build()` computes `_gameDisplaySize` by containing 16:9 into available
  overlay bounds, then uses `Center + SizedBox(width,height) + Texture`.
- `_GameSurfaceLayer` is only inside that `SizedBox`, so letterbox bars outside
  the game rectangle are not part of the game hit target.

Do not reintroduce:

- scheduling `SurfaceTexture` with `_gameDisplaySize * devicePixelRatio`;
- using `Positioned.fill` texture as the game buffer owner;
- native viewport metrics as the input mapping authority;
- Flutter and native both trying to own visible letterboxing.

### Android Kotlin host

File:

- `platforms/android/app/java/org/github/krkr2/MainActivity.kt`

Important rules now encoded:

- `GAME_SURFACE_WIDTH = 1920`
- `GAME_SURFACE_HEIGHT = 1080`
- `createGameSurfaceTexture()` ignores arbitrary requested size and calls
  `SurfaceTexture.setDefaultBufferSize(1920, 1080)`.
- `resizeGameSurfaceTexture()` also ignores arbitrary requested size and keeps
  `1920x1080`.
- Logs include the requested size for diagnostics, but the actual size remains
  fixed.

### Android JNI/native surface state

File:

- `platforms/android/cpp/krkr2_android.cpp`

Important rules now encoded:

- `TVPAndroidGetFlutterGameSurfaceSize()` reports `1920x1080` only when a valid
  Flutter game surface window exists.
- `nativeSetGameSurface()` stores and applies fixed `1920x1080`, regardless of
  Java's requested dimensions.
- `nativeResizeGameSurface()` stores and applies fixed `1920x1080`, regardless
  of Java's requested dimensions.
- Detach still clears the stored surface size to `0x0`.

### Shared native constants

File:

- `cpp/core/environ/sdl/SDLPresentTypes.h`

Constants:

- `kTVPSDLFixedGameSurfaceWidth = 1920`
- `kTVPSDLFixedGameSurfaceHeight = 1080`

Use these for SDL/Android presenter output decisions. Do not hand-spell another
runtime surface size unless it is clearly only a local Flutter visual size.

### SDL/Android render plan

File:

- `cpp/core/environ/sdl/SDLGameManager.cpp`

Important rule now encoded:

- Android takeover `TVPSDLTryPresentTexture()` builds
  `TVPSDLTexturePresentPlan.outputWidth/outputHeight` from the fixed constants,
  not from Flutter overlay size or texture size fallback.

### Android Flutter presenter

File:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Important rules now encoded:

- `TryPresentAndroidTexturePlan()` uses fixed `1920x1080` for the final output
  viewport calculation.
- `TryPresentAndroidEGLSurfaceTexture()` clamps output size to fixed
  `1920x1080` before creating/recreating the EGL window surface, setting GL
  viewport, clearing, drawing, and remembering pending presented size.
- `TVPSDLAndroidFlutterPresenterTryPresentTexture()` direct fallback uses fixed
  output size.
- `TVPSDLAndroidFlutterPresenterTryPresentSurface()` direct software-surface
  fallback uses fixed output size.
- `TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize()` clamps
  remembered presented dimensions to fixed `1920x1080` on Android, preventing
  stale full-screen dimensions from becoming the input/presenter authority.

## Render-chain rule to keep

The earlier Aether-style frame-end swap work remains correct and should stay:

1. Engine produces a real new frame.
2. External presenter draws the full output back buffer.
3. EGL path marks `frameDirty`.
4. Java `Cocos2dxRenderer.onDrawFrame()` calls native frame end after
   `nativeRender()`.
5. Native frame end calls `TVPSDLAndroidSwapExternalPresenterIfDirty()`.
6. Only a real dirty external frame is swapped.
7. Runtime present info and dirty rect consumption happen only after successful
   external post/swap.

The corrected fixed-size contract changes only the output dimensions and
Flutter display ownership. It does not remove the "new frame only then swap"
rule.

## Performance rule

The user explicitly complained about too much validation in the graphics path.
Keep hot path lean:

- no checksums;
- no `glReadPixels` validation;
- no per-frame graphical integrity scans;
- no extra `glGet*` unless under diagnostics;
- dirty rect is upload/cache metadata only;
- visible present remains deterministic full-frame.

The desired path is still:

- native GL texture when available;
- full-frame GPU present to fixed output;
- CPU/direct fallback only for compatibility.

## Future migration notes

The project still has Cocos2dx bootstrap/lifecycle pieces. The target remains
removing Cocos from the final runtime path. Future agents should continue by
moving lifecycle/input/presentation ownership to Flutter + SDL3, but must not
break this fixed-buffer rule while doing so.

