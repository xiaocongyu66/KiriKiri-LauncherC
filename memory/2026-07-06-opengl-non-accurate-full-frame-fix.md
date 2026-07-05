# 2026-07-06 OpenGL non-accurate full-frame fix

User reported the duplicated toolbar / stale strip artifact is caused by the
OpenGL rendering pipeline when `ogl_accurate_render` is disabled.

Change made in `KiriKiri-LauncherC` only:

- `cpp/core/visual/LayerIntf.cpp`
  - For Android runtime screen takeover, the non-accurate GPU path now completes
    the full local layer rect instead of only `updateRegion.GetBound()`.
  - This applies to both window completion and cache completion.
  - Rationale: the Flutter SurfaceTexture presenter samples the native GL
    texture as a complete frame. Partial GPU completion can leave stale pixels in
    regions outside the dirty union, which matches the screenshot artifact.

- `cpp/core/environ/sdl/SDLGameManager.cpp`
  - When Android screen takeover presents a GL-backed texture, partial dirty
    rects are expanded to the full source frame before present planning.
  - This keeps native GL handoff semantics aligned with the EGL presenter, which
    blits the whole texture to the SurfaceTexture each frame.

Expected log change:

- `queue-android-egl ... rect=0,0,1920x1080 ... fullFrame=1`
  should be seen for native GL takeover frames instead of partial menu/strip
  dirty rectangles.

Validation status:

- No local compile was possible in this container because `cmake`, `java`,
  `ninja`, `clang++`, `g++`, `c++`, and `clang-format` are not on `PATH`.
  Running the Android wrapper failed immediately with `JAVA_HOME is not set and
  no 'java' command could be found in your PATH`.
  Build should be verified with:

```sh
./platforms/android/gradlew -p ./platforms/android assembleDebug
```
