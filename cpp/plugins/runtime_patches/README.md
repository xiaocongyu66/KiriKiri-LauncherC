# Runtime Compatibility Patches

This directory hosts TJS-level fallback scripts that are injected before
`patch.tjs` is executed on every game startup. They exist because the krkr2
Android engine cannot ship every closed-source plugin a game expects (most
notably the Wamsoft chain: `wfBasicEffect`, `wfTypicalDSP`, `wumultitrack`,
`wuopus`, `wuvorbis`, `wvdecoder`).

## Loader

The loader lives in `cpp/plugins/runtimePatchLoader.cpp`. It is called from
`TVPExecuteStartupScript` and runs in this order:

1. **Built-in default** — the contents of `builtin_default.tjs`, embedded
   into the binary at build time. Only injected when the user does not
   place a sentinel file `<gamedir>/compat/no_default` next to the game.
2. **Per-game overrides** — every `*.tjs` file under `<gamedir>/compat/`
   is executed in lexical order. Files ending in `.disabled.tjs` are
   skipped, which gives users a way to keep them around without running.

All scripts are wrapped in a try/catch, so a broken patch only logs an
error (visible via `logcat`/`spdlog`) and never blocks the launcher from
starting.

## Authoring rules

* Never overwrite a real engine or game-side object. Always wrap additions
  in `if(typeof global.<name> == "undefined")` (or the `__krkr2_hasGlobal`
  helper supplied by the built-in default).
* Make every method a no-op or return a benign default; the goal is "the
  game keeps running with reduced functionality", not "the game thinks the
  feature works".
* Group additions under a comment that names the upstream plugin or
  scenario, so it is easy to remove fallback once a real implementation
  lands.
* Store new built-in fallbacks as separate `*.tjs` files in this
  directory and update the `RUNTIME_PATCH_FILES` entry in
  `cpp/plugins/CMakeLists.txt` so the build embeds them.

## Per-game user overrides

Game directory layout:

```
<gamedir>/
├── data.xp3
├── patch.tjs
└── compat/
    ├── no_default          # optional: disable the built-in fallback
    ├── 10-add-Foo.tjs      # custom fallback, runs before patch.tjs
    └── 90-misc.disabled.tjs  # kept on disk but skipped
```

Numeric prefixes are recommended for ordering, mirroring how systemd
enumerates `*.d/` directories.