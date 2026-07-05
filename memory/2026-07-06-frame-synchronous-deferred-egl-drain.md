# 2026-07-06 Frame-synchronous deferred EGL drain

## Context

User provided new logs:

- `/root/log/78.log`
- `/root/log/20260706020346492.log`
- `/root/log/20260706020529280.log`

The previous diagnostics from `c68d566 Add render sync diagnostics` proved that
the Android EGL presenter itself can create a fixed `1920x1080` Flutter surface
and draw full-frame GL output, but the swap cadence was still wrong.

Important log evidence:

- `surface ready ... size=1920x1080 fullFrame=1`
- `queue-android-egl ... dirtySerial=N`
- many `sync-overwrite-android-egl`
- at sampled swaps, `dirtyMarks` was greater than `swapAttempt` and
  `dirtyOverwrites` kept increasing.

Example pattern from `20260706020529280.log`:

- a dirty frame is queued in `BasicDrawDevice::Show`
- another `BasicDrawDevice::Show` queues a newer dirty frame before
  `TVPForceSwapBuffer` swaps the previous one
- sampled swap later shows `dirtySerial=43 swapAttempt=32 dirtyMarks=43
  dirtyOverwrites=11`

This means multiple full-frame EGL blits were being made into the external
SurfaceTexture backbuffer before the single Cocos/Java frame-end swap happened.
Only the last queued backbuffer reached `eglSwapBuffers`. That explains the
user-visible "only newest area is current, other areas are stale" symptom.

## Reference rule

AetherKiri does not rely on arbitrary idle/frame-end swaps:

1. `UpdateDrawBuffer()` draws the complete game frame into the EGL target.
2. It marks the EGL target dirty only after the draw succeeds.
3. `TVPForceSwapBuffer()` swaps only if that dirty flag is set.
4. A tick owns this sequence, so one produced game frame is paired with one
   deterministic swap.

krkrsdl3 has a similar deterministic owner:

1. run engine tick,
2. draw complete backbuffer,
3. swap once.

LauncherC was missing that pairing because the transitional Cocos host can call
`BasicDrawDevice::Show` multiple times before `nativeFrameEnd`.

## Change

Edited only active project files:

- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.h`
- `cpp/core/environ/sdl/SDLAndroidFlutterPresenter.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

Reference projects were not edited.

### 1. Keep the low-level presenter Aether-style deferred

`TryPresentAndroidEGLSurfaceTexture()` still:

- binds the fixed `1920x1080` Flutter EGL window surface,
- full clears it opaque black,
- draws the full source texture through the aspect viewport,
- `glFlush()`es,
- restores the previous EGL current state,
- marks `state.frameDirty = true`,
- stores pending texture/frame metadata,
- does **not** directly call `eglSwapBuffers()`.

This keeps the "draw then mark dirty" model instead of posting from the low
level blit helper.

### 2. Expose stage-aware swap drain

Added:

```cpp
bool TVPSDLAndroidFlutterPresenterSwapIfDirty(const char *stage);
```

`TVPSDLAndroidSwapExternalPresenterIfDirty()` now delegates to it with
`"TVPForceSwapBuffer"`.

This lets the SDL runtime presenter drain the dirty EGL frame with the real
producer stage (`BasicDrawDevice::Show`) in logs, while keeping the old C ABI
entry point for Android platform/frame-end callers.

### 3. Drain immediately after pending frame info is registered

In `TVPSDLTryPresentTexture()` after an Android EGL deferred present succeeds:

1. The runtime frame info is written into
   `gSDLScreenPresenterState.pendingExternalFrameInfo`.
2. Immediately call `TVPSDLAndroidFlutterPresenterSwapIfDirty(stage)`.
3. If swap succeeds, call `TVPSDLRecordExternalPresenterPostedFrame()` right
   away.

This is the important sync fix: the dirty EGL frame is still produced by the
Aether-style deferred path, but it is consumed at the same KiriKiri produced
frame boundary instead of waiting for a later Cocos/Java frame-end that may be
after several more `Show()` calls.

Native frame end remains a fallback:

- if the immediate drain already swapped, later `nativeFrameEnd` sees clean
  state and no-ops;
- if the immediate drain fails or the surface is unavailable, pending state is
  left for the existing frame-end path to attempt.

### 4. New log line

Added sampled/diagnostic sync log:

```text
[sdl-sync] drain-deferred sequence=N stage=BasicDrawDevice::Show path=egl swapped=1 overwrite=0
```

Expected healthy pattern:

- `queue-android-egl #N ... overwrite=0`
- `swap-android-egl #N stage=BasicDrawDevice::Show ... dirtySerial=N ... dirtyOverwrites=0`
- `posted record=N sequence=N predicted=N ...`
- `drain-deferred sequence=N ... swapped=1 overwrite=0`

The key target for the next user log is that `dirtyOverwrites` should stop
climbing during normal text/image advance. Occasional clean `TVPForceSwapBuffer`
later is acceptable because the immediate drain already posted the frame.

## Why not immediate low-level swap here

A previous immediate-swap approach posted from inside the Android EGL blit
helper before the upper runtime presenter recorded pending frame info. That made
the ownership unclear.

This pass keeps ownership cleaner:

- low-level EGL helper: render and mark dirty;
- SDL runtime presenter: pair pending frame metadata with the dirty swap;
- Android platform `TVPForceSwapBuffer`: compatibility fallback only.

This better matches AetherKiri's "new frame marks dirty, swap consumes dirty"
rule while adapting to LauncherC's transitional Cocos host, where `Show()` can
happen more than once before Java frame end.

## Hard constraints still in force

- Game/native surface remains fixed `1920x1080`.
- Final visible present remains full-frame deterministic, not partial-region
  Android compositor present.
- Dirty rects are internal upload/trigger hints only.
- No hot-path `glReadPixels`, checksums, pixel validation, or image integrity
  verification.
- Continue migration toward Flutter + SDL3 and remove Cocos from the final
  game-frame ownership path.
