# 2026-07-05 full-surface Flutter/SDL3 presenter continuation

## Superseded correction

This note records a design that was immediately corrected by the user.
Do **not** follow the "full-surface Flutter output buffer" rule below as the
current target.

Current hard rule as of the later 2026-07-05 continuation:

- The actual game/native external presenter buffer is fixed `1920x1080`.
- Flutter may contain/center that buffer visually, but must not resize the
  `SurfaceTexture`/native `ANativeWindow` output to the full overlay or screen.
- Native SDL/EGL presenter output dimensions are fixed `1920x1080`.
- Input from Flutter maps from the centered/contained displayed rectangle back
  to fixed `1920x1080` coordinates.
- See `memory/2026-07-05-fixed-1920x1080-game-surface-contract.md`.

## Scope and hard target

Only modify:

- `/root/kiriki-work/KiriKiri-LauncherC`

Reference-only projects:

- `/root/kiriki-work/AetherKiri`
- `/root/kiriki-work/krkrsdl2-main`
- `/root/kiriki-work/krkrsdl3-main`
- `/root/kiriki-work/docs`
- `/root/kiriki-work/SDL-release-3.4.10`

The hard target remains:

- Migrate to Flutter + SDL3.
- Remove Cocos2dx from the final game render path.
- Keep the render path complete, high performance, and high compatibility.
- Prefer whole proven reference patterns from AetherKiri / krkrsdl3 over
  fragmented experiments.
- Do not add hot-path graphical integrity validation, checksums, or
  `glReadPixels` diagnostics unless explicitly gated behind diagnostics.

## Why this continuation was needed

The previous pass moved Android EGL present toward the AetherKiri rule:

`produce frame -> draw external EGL back buffer -> frame-end dirty swap -> only then record posted frame`

However, `flutter_launcher/lib/src/pages/game_overlay_page.dart` still had a
dimension feedback hazard:

- Native returned a presented output size.
- Flutter could treat that size as the requested `SurfaceTexture` size.
- For a fixed 1920x1080 game on a wider device, this allowed the content frame
  size and output surface size to collapse into the same concept.
- That is exactly the class of bug that can produce old/new split frames,
  shifted layers, or wrong viewport feedback.

The correct model is now:

- `contentWidth/contentHeight`: game texture/content frame, for aspect and
  input mapping only.
- `surfaceWidth/surfaceHeight`: requested Flutter `SurfaceTexture` / external
  output buffer size.
- `presentedWidth/presentedHeight`: diagnostic output/post size only, never a
  resize authority.
- `viewportX/Y/W/H`: native aspect viewport inside the output surface.

## Flutter changes

File:

- `flutter_launcher/lib/src/pages/game_overlay_page.dart`

Changes:

- Added native viewport state:
  - `_nativeViewportX`
  - `_nativeViewportY`
  - `_nativeViewportWidth`
  - `_nativeViewportHeight`
- `_refreshGameSurfaceMetrics()` now reads:
  - `contentWidth/contentHeight`
  - `surfaceWidth/surfaceHeight`
  - `viewportX/Y/W/H`
  - legacy `width/height` only as content fallback.
- `_refreshGameSurfaceMetrics()` no longer calls `_ensureGameSurface()` from
  presented or content dimensions.
- `_gameSurfaceWidth/_gameSurfaceHeight` are synchronized from native surface
  dimensions only when not in a resize/create request.
- The Flutter `Texture` now fills the whole available game overlay area.
- Flutter schedules the `SurfaceTexture` buffer from the actual overlay bounds
  times DPR, not from the 1920x1080 content frame.
- Native now owns the final aspect viewport and clears/draws inside that full
  output surface.

Important rule:

- Do not put the game `Texture` back inside a content-sized `Center/SizedBox`
  as the final architecture. That creates a second viewport owner in Flutter.
  The current rule is one visible viewport owner: native SDL/EGL presenter.

Input mapping:

- `_toSurfacePosition()` now uses `_nativeViewportX/Y/W/H` when available:
  - local Flutter full-surface pointer position -> physical output position;
  - subtract native viewport offset;
  - scale to content coordinates;
  - clamp to content frame.
- If viewport metrics are not available yet, it falls back to full-surface
  mapping.

## Android channel/native metrics changes

Files:

- `platforms/android/app/java/org/github/krkr2/MainActivity.kt`
- `platforms/android/cpp/krkr2_android.cpp`

Changes:

- `getGameSurfaceMetrics` now returns a map with separate keys:
  - `width`, `height` as compatibility aliases for content size;
  - `contentWidth`, `contentHeight`;
  - `presentedWidth`, `presentedHeight`;
  - `surfaceWidth`, `surfaceHeight`;
  - `viewportX`, `viewportY`, `viewportWidth`, `viewportHeight`.
- `nativeGetGameSurfaceMetrics()` now returns a 10-int array:
  1. presentedWidth
  2. presentedHeight
  3. flutter surface width
  4. flutter surface height
  5. content texture width
  6. content texture height
  7. dest viewport x
  8. dest viewport y
  9. dest viewport width
  10. dest viewport height
- Content and viewport metrics come from
  `TVPRuntimeGetPresentFrameInfo()`.
- Native only exposes content/viewport metrics when there is a remembered
  posted/presented surface size. After surface set/resize/detach clears
  presented size, stale frame info is not used as current viewport feedback.
- Presented size remains diagnostic and is not allowed to drive Flutter
  `SurfaceTexture` resize.

## Direct Android fallback changes

File:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`

Why:

- The high-performance Android EGL path already takes `textureWidth/Height`
  plus `outputWidth/Height`, clears the full output, computes aspect viewport,
  and draws the whole texture.
- The direct `ANativeWindow_lock/unlockAndPost` fallback still assumed
  `source size == buffer size`.
- With a full-screen Flutter surface, that fallback also needs deterministic
  full-output behavior or it can reintroduce stretch/shift/old-area issues.

Changes:

- Added `FillAndroidBufferBlack()`.
- Added `CopyTextureToAndroidBufferViewport()`:
  - clears the full output buffer;
  - computes the same aspect viewport;
  - scales/copies source texture scanlines into the viewport.
- Added `CopySurfaceToAndroidBufferViewport()` with the same behavior for SDL
  software surfaces.
- `TVPSDLAndroidFlutterPresenterTryPresentTexture()` now:
  - uses Flutter game surface size as output size;
  - sets `ANativeWindow` buffers geometry to output size;
  - locks/posts the full output;
  - copies the source into native aspect viewport;
  - remembers output size after post.
- `TVPSDLAndroidFlutterPresenterTryPresentSurface()` now follows the same
  output-size and viewport-copy rule.
- `TryPresentAndroidTexturePlan()` now reports direct fallback `destRect` as
  the computed output viewport, not the source rect.

This keeps direct fallback semantically aligned with the EGL path:

`source content frame != output surface != diagnostic presented size`

## Current presenter ownership rule

Native owns final present:

1. Flutter provides a full output `SurfaceTexture` sized from overlay bounds.
2. Native presenter receives the output size via
   `TVPAndroidGetFlutterGameSurfaceSize()`.
3. GL path:
   - prepare texture for external presenter;
   - bind external EGL surface;
   - clear full output;
   - compute aspect viewport;
   - draw full texture quad;
   - defer swap until frame end;
   - record posted frame only after successful swap.
4. Direct fallback:
   - lock full output;
   - clear/copy source into same aspect viewport;
   - unlock/post synchronously;
   - record posted frame immediately.
5. Flutter only displays the full texture and maps pointer input through
   native viewport metrics.

## Do not regress

- Do not make presented size drive Flutter resize.
- Do not let content size drive Flutter `SurfaceTexture` resize.
- Do not reintroduce visible partial present.
- Do not rely on preserved swap behavior.
- Do not consume dirty rect before a real post/swap succeeds.
- Do not count EGL plan acceptance as a presented frame.
- Do not let Flutter and native both own aspect letterboxing.
- Do not bring raw `extern TVPSetRenderTarget(GLuint)` into presenter code;
  keep external detach inside `PrepareTextureForExternalPresenter()`.

## Verification

Local verification:

- `git diff --check` passed after these edits.

Not run locally:

- Android/Gradle/Flutter build, because this environment has no local
  `java`, `cmake`, `ninja`, `clang++`, `flutter`, `dart`, or `kotlinc` on PATH.

## Next test focus

Watch the next `78.log` or device run for:

- `MainActivity.createGameSurfaceTexture` should create/resize to the full
  Flutter overlay physical size, such as `2780x1264`, not to `1920x1080`
  because a frame was presented.
- `android-egl-presenter surface ready` should use the same full output size.
- `present-android-egl` diagnostics, if enabled, should show
  `texture=1920x1080 output=<full surface> viewport=<aspect rect>`.
- No old/new split: sidebars/top-bottom outside viewport must be black from
  full-output clear, not stale buffer contents.
- Touch input should land correctly inside the native viewport; black bars
  should clamp to content edges.
