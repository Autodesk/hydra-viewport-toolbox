# Git Hooks

Optional local git hooks for faster feedback before CI. Hooks live in `.githooks/` and are
**opt-in per clone** — git never installs them automatically.

## Pre-push build hook

`.githooks/pre-push` runs a local build check when you push a branch, so compile breakage is
caught before it reaches CI:

- **What runs:** configure + build for a lean preset set — `debug` (build-only), `asan` and
  `ubsan` (build **plus the unit-test suite**, so memory errors, undefined behavior, and test
  regressions surface before CI). The suite runs as a single `hvt_test` process (one
  GPU/engine startup) rather than through `ctest`, which spawns one process per test case —
  far faster on macOS.
- **When:** every `git push` to a non-`main` branch — new branches and new commits on existing
  PR branches alike. Pushes to `main`/`master`, tag pushes, and branch deletions are skipped.
- **On failure:** the push is aborted with the failing preset and step named.

The builds are incremental (each preset keeps its own `build/<preset>/` tree), so the hook is
fast after the first run. Tests run for the sanitizer presets only — they need a GPU/display —
so `debug` stays build-only; see [testing.md](testing.md) for the test workflow and CI for
full validation.

## Setup (once per clone)

```bash
git config core.hooksPath .githooks
```

That's all — git now runs every executable script in `.githooks/` matching a hook name
(`pre-push`, `pre-commit`, …). Verify with:

```bash
git config core.hooksPath        # -> .githooks
```

## Customizing

- **Presets:** edit the `PRESETS` array at the top of `.githooks/pre-push` (e.g. add `ubsan`,
  `release`). Add a preset name to `TEST_PRESETS` to also run its unit tests. Preset
  definitions live in [CMakePresets.json](../CMakePresets.json) — the sanitizer presets are
  Linux/macOS only, and the Vulkan presets require the Vulkan SDK.
- **Bypass once:** `git push --no-verify`.
- **Disable:** `git config --unset core.hooksPath`.

## Limitations

Local hooks are a convenience layer, not enforcement. They are opt-in per developer and can be
bypassed with `--no-verify`, so they never replace CI and branch protection on `main` — they
just make the common failure (a preset that does not compile locally) cheap to catch before
the push.

## Windows

The hook works under Git for Windows (hooks run through its bundled bash, even when pushing
from PowerShell or cmd):

- The sanitizer presets are trimmed automatically — they are not defined for Windows in
  [CMakePresets.json](../CMakePresets.json), so the hook builds `debug` only there.
- `.gitattributes` forces LF endings for `.githooks/*`; a CRLF shebang would fail before the
  script starts.
- Push from the **x64 Visual Studio developer environment** so `cl`/`ninja` are on `PATH` —
  the same requirement as any local Windows build.
