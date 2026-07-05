# 2026-07-06 render sync diagnostics

## User request

Add more render logs, especially around frame synchronization, then push.

## Hard constraints still active

- Target architecture remains Flutter + SDL3.
- Keep game/native surface fixed at 1920x1080.
- Do not add hot-path pixel validation, readbacks, checksums, or heavy
  defensive logic.
- Continue removing Cocos gradually; this change only adds diagnostics to the
  current transitional presenter path.

## Files changed

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

## What was added

### Android EGL presenter sync serials

`TVPAndroidEGLSurfacePresenterState` now tracks lightweight counters:

- `dirtyMarks`: every time the EGL presenter renders a frame into the external
  EGL surface and marks it dirty for the frame-end swap.
- `pendingDirtySerial`: serial for the dirty frame currently waiting for
  `eglSwapBuffers`.
- `dirtyOverwrites`: increments when a new dirty frame is queued before the
  previous dirty frame was swapped.
- `cleanSwapChecks`: counts frame-end swap calls with no dirty frame. This is
  logged only when `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS` is enabled and sampled.
- `swapAttempts`: every actual dirty swap attempt.
- `missingSurfaceDirtyDrops`: dirty frames dropped because the EGL surface was
  gone before swap.

`queue-android-egl` logs now include:

- `dirtySerial`
- `overwrite`
- `oldDirty`
- fixed output size, viewport, source rect, upload rect, native GL texture id,
  software upload state, UV scale, flipY, and fullFrame.

`swap-android-egl` logs now include:

- `dirtySerial`
- `swapAttempt`
- `dirtyMarks`
- `dirtyOverwrites`
- `nativeGL`
- `nativePresents`

This makes it possible to match a queued frame with the later frame-end swap.

### Dirty overwrite diagnostics

When a new EGL present overwrites an unswapped dirty frame, a sampled default log
is emitted:

```text
[android-egl-presenter] sync-overwrite-android-egl #... oldDirty=... newDirty=...
```

This is important for debugging the user-reported "new/old frame split" issue.
If this log appears frequently, the current transitional host is producing
multiple external EGL draws between frame-end swaps.

### Clean swap diagnostics

When `TVPSDLAndroidSwapExternalPresenterIfDirty()` is called without a dirty
EGL frame, diagnostics mode logs sampled:

```text
[android-egl-presenter] swap-skip-clean #...
```

This stays behind `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS` because it can happen
every frame.

### SDL frame sync logs

Added a new native log tag:

```text
[sdl-sync]
```

It logs the high-level relationship between:

1. `TVPSDLTryPresentTexture()` queuing a deferred Android EGL frame;
2. `TVPForceSwapBuffer()` / Java `nativeFrameEnd()` actually swapping it;
3. `TVPSDLRecordExternalPresenterPostedFrame()` recording it as the runtime
   visible frame.

New sampled messages:

- `queued sequence=... path=egl ... overwrite=...`
- `posted record=... sequence=... predicted=...`
- `posted-immediate sequence=... path=direct`
- `posted-missing-info ...`
- diagnostics-only `present-skip no-dirty` / `present-skip empty-dirty`

The deferred path stores `pendingExternalPredictedSequence`; after swap, the
posted log shows whether the predicted and actual visible-frame sequence match.

## Why this is safe

- No new pixel readback.
- No `glGet*` or validation was added.
- Success logs are sampled by the existing `ShouldLogScreenPresenter` schedule
  rather than emitted every frame.
- The only per-frame diagnostics-only log added is the no-dirty swap skip; it is
  gated by `KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS` and sampled.
- The counters are plain integers updated under existing presenter mutexes.

## Expected useful log correlation

For a healthy deferred EGL frame:

```text
[android-egl-presenter] queue-android-egl #10 ... dirtySerial=10 ...
[sdl-sync] queued sequence=10 ... path=egl ...
[android-egl-presenter] swap-android-egl #10 ... dirtySerial=10 ...
[sdl-sync] posted record=10 sequence=10 predicted=10 ...
```

If old/new split happens, look for:

- frequent `sync-overwrite-android-egl`;
- `queued ... overwrite=1`;
- mismatched `posted ... predicted=... sequence=...`;
- `posted-missing-info`;
- `swap-drop-no-surface`.

These logs should make it clear whether the issue is:

- multiple EGL draws before one frame-end swap;
- frame-end swap running without matching pending runtime frame info;
- surface recreation between draw and swap;
- no-dirty frame-end calls.
