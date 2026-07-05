# 2026-07-05 Flutter SurfaceProducer game surface

## Why

The 18:45 Android log has Flutter's warning:

`Flutter recommends migrating plugins that create and register surface textures to the new surface producer API`

The same startup area also has Mali gralloc `Usage not permitted` / unsupported
format noise while the Flutter texture surface is being created. The old
`createSurfaceTexture()` path can choose a buffer producer/consumer contract
that is not ideal for Android 14+ and new Flutter embeddings.

## Change

Edited only:

- `platforms/android/app/java/org/github/krkr2/MainActivity.kt`
- `platforms/android/app/proguard-rules.pro`

The Flutter overlay game surface creation now tries this order:

1. Reflectively call `FlutterRenderer.createSurfaceProducer()` when the current
   embedding provides it.
   - If the no-arg default method is unavailable, it tries
     `createSurfaceProducer(TextureRegistry.SurfaceLifecycle.manual)`.
2. Set the fixed game buffer size to `1920x1080`.
3. Use `SurfaceProducer.getSurface()` as the native game `Surface`.
4. Register a reflected `SurfaceProducer.Callback`.
5. Fall back to the legacy `createSurfaceTexture()` / `SurfaceTextureEntry`
   path if the API is unavailable or fails.

The reflection keeps older Flutter embeddings source-compatible while allowing
new builds to avoid the deprecated SurfaceTexture plugin path.

Release minification now keeps the Flutter renderer and SurfaceProducer API
members that are reached through reflection.

## Lifecycle behavior

`GameSurfaceTarget` now owns either:

- a `surface-producer` target, or
- a legacy `surface-texture` target.

For `surface-producer`:

- `onSurfaceAvailable` / older `onSurfaceCreated` reacquires the current
  `Surface` and calls `nativeSetGameSurface(...)`.
- `onSurfaceCleanup` / older `onSurfaceDestroyed` calls
  `nativeDetachGameSurface()` when the target is active.
- dispose clears the callback and releases the producer.
- resize calls `setSize(1920, 1080)`, reacquires `getSurface()`, and calls
  `nativeSetGameSurface(...)` again because the producer may replace its
  underlying surface.

For legacy `surface-texture`:

- behavior stays close to the old implementation.
- the Java-created `Surface` is still explicitly released.

The method-channel response now includes `"surfaceMode"` for diagnostics, but
the existing Dart caller can ignore it.

Creating a new game surface disposes any previously registered game-surface
target first, so repeated overlay creation cannot leave stale Java surfaces or
native windows alive.

## Escape hatch

Set:

```sh
KRKR2_DISABLE_FLUTTER_SURFACE_PRODUCER=1
```

to force the old `SurfaceTextureEntry` path if a device/embedding combination
has a SurfaceProducer-specific issue.

## Verification

Local Android toolchain is still not usable in this container:

- `java` not found
- `kotlinc` not found
- Gradle wrapper exists but cannot run without Java

Ran:

```sh
git diff --check
```

Result: clean.
