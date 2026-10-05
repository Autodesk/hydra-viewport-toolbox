---
name: review-hvt-pr
description: >-
  HVT code-review conventions. Use when reviewing a pull request or branch diff
  in this repo, or self-reviewing before opening a PR — the review procedure
  and severity order, and the issues HVT reviewers repeatedly flag: compiler
  pragmas and C++17 portability, OpenUSD version guards, Hydra task dirty bits
  and params, Hgi resource lifetime, error handling, test and baseline quality,
  CMake/CI, comment verbosity, and PR scope/merge gates. Read before reviewing
  a PR or before asking for review.
---

# HVT code-review conventions

These conventions come from the review history of the last ~100 PRs (#114–#214). Each item cites
the PRs where reviewers raised it. Apply them when reviewing, or when self-reviewing before
opening a PR. House style is in the `openusd-coding-style` skill and task structure in
`create-hvt-task`, so they are not repeated here.

## Procedure

1. Read the PR description, then the full diff (`gh pr diff <n>` or `git diff main...`).
2. Go through the sections below. For each finding give the `file:line`, a concrete failure
   scenario (inputs or platform → wrong output, crash, or build break) and a fix.
3. Rank findings: **build break / correctness / leak** > **portability / USD version** >
   **tests** > **API / design** > **comments / style**. Drop anything `clang-format` would fix.
4. Flag only lines the PR touches. Reviewers object to restyling untouched code (#153, #171).

## Portability

- **Compiler pragmas:** guard Clang-only warnings (`-Wgnu-zero-variadic-macro-arguments`,
  `-Wc++20-extensions`) with `#if defined(__clang__)` and `#pragma clang diagnostic`, **never**
  `__GNUC__`. Clang also defines `__GNUC__`, and GCC on Linux then fails with `-Werror=pragmas`
  (#181). Every `push` needs a `pop` on every branch, MSVC included (#171).
- **Pragma scope:** push/pop wraps only the offending `#include`s. Fix warnings in private code
  instead of silencing them in CMake. Public headers must compile warning-free (#171).
- **C++17 only:** no `<compare>`, designated initializers or other C++20 features (#156).
- **Preprocessor directives** are not indented (#149, #171).
- **Paths:** watch for `abc//cde` and trailing-slash differences between Windows and *nix (#122).

## OpenUSD versions

- HVT builds against USD 24.11 (MayaHydra) through the latest dev. New Hydra/USD API needs a
  `PXR_VERSION` guard (#179 broke 26.05, #183, #199).
- **Guard direction** must match the baseline folder it selects (#149 had `<= 2603` for `>= 2511`).
- Avoid version `#if`s in public headers, because callers then need them too (#183).
- **Code copied from OpenUSD `hdx`:** diff it against upstream. #129 silently dropped
  `stencilTestEnabled=false` and `alphaToCoverageEnable=false`.

## Hydra tasks

- **Dirty bits:** every early `return` in `_Sync` decides what happens to dirty bits. Clearing them
  after a failed params fetch latches the failure. Not clearing them re-syncs every frame. Apply
  the fix to all **sibling tasks** too (#186, #187).
- **Params:** `operator==` covers every field. Do not hand-compare fields elsewhere (#186).
- **Setters** that write local params get overwritten by the next `_Sync` and never take effect
  (#186).
- **Tokens:** use the shared tokens / `GetToken()` for task and `HdTaskContext` names, not string
  literals. Do not build `TfToken("…")` per frame or inside loops (#171, #186).
- **Install twice:** a second manager on the same `FramePass` must not half-fail silently. Check
  `HasTask` before `AddTask` (#186).
- **AOV buffers:** sizes and MSAA sample counts must match before a copy or reuse. Include
  resolve buffers, and check every buffer rather than only depth (#184, #213).
- **Render outputs** belong to the caller. Explicit `renderOutputs` win over anything `FramePass`
  infers (#126, #137, #144).

## GPU resources and concurrency

- Destroy Hgi resources on failure paths (shader compilation fails, `_renderIndex` is null) and
  when a feature is switched off (#129, #186).
- Do not rebuild pipelines or textures every frame or every resize when nothing changed (#186).
- Check for raw pointers that can outlive their owner and for `HgiUniquePtr`/`shared_ptr`
  mismatches (#186, #190).
- No mutable `static` GPU caches; use members. Use the right atomic ordering, and RAII for
  counters so exceptions don't leak them (#158, #181).
- Pass ref-counted handles and large params by `const&` (#122, #136).

## Error handling

- Factories must not return `nullptr` to callers that dereference it. Use `TF_CODING_ERROR` or
  `TF_FATAL_ERROR`, or check at the call site (#183).
- Use `TF_RUNTIME_ERROR`/`TF_CODING_ERROR` with a specific message. Use `TF_DEBUG` rather than
  `TF_WARN` for supported configurations, and latch warnings that would repeat every frame (#171,
  #186).

## Tests and baselines

- **A behavior change or fix needs a test that fails without it.** If no test is feasible, the PR
  says why (#184, #213).
- **Assert something real.** Comparing against the constructor default proves nothing.
  `TF_CODING_ERROR` does not fail a gtest, so wrap the code in `TfErrorMark` (#186).
- **Baselines must discriminate:** several prim IDs, asymmetric framing, a zoom where the effect
  shows. Every changed baseline needs a reason in the PR (#122, #129, #134, #186). Check that the
  computed baseline name actually reaches `validateImages` (#147).
- **Hygiene:** no console output by default, no silenced errors, and global state (cwd, env vars)
  restored with an RAII guard (#151, #154, #175).
- **Platform skips and thresholds** apply only to the affected backend, carry a comment, and use a
  `HgiDeviceCapabilitiesBits…` check over platform macros where possible (#117, #149, #181).

## CMake and CI

- Prefer presets over new CMake logic. Turn repeated blocks into a helper (#123, #198).
- New CMake variables, env vars and macros use an `HVT_` prefix, with matching names across all
  three. No downstream product names (#162, #183, #197).
- Use `PROJECT_BINARY_DIR`, not `CMAKE_BINARY_DIR`. List new headers in the right
  `_PUBLIC`/`_PRIVATE_HEADER_FILES` (#149, #186).
- Test-helper libraries are static, with no gtest dependency (#196, #197, #200).
- CI cleanup steps use `if: always()`. External contributors never trigger GPU runs on unreviewed
  commits (#125, #143).

## Comments and API

- **Say the why, not the what.** Remove verbose or AI-generated narration, multi-line comments on
  trivial code, and stale Doxygen. Explain magic numbers in one line (#177, #186, #198, #199).
- No commented-out code and no unused functions or parameters (#116, #157, #159).
- Is each new public type necessary? Does each name match what the code does (`_ApplyX` that only
  returns becomes `_GetX`)? Adding `[[nodiscard]]` to an existing API breaks `-Werror` callers
  (#153, #183, #186).
- A large design gets a `docs/*.md`. When behavior or the recommended entry point changes, update
  `docs/`, `test/README.md` and the `AGENTS.md` routing (#183, #186).

## PR scope and merge gates

- **One topic per PR.** No drive-by reformatting, moved lines or accidental files (#126, #153,
  #187, #199).
- The description matches what shipped, including behavior changes and baseline moves. Update it
  after review changes (#186, #199).
- Engine or API changes wait for the downstream builds to pass before merging (#154, #171, #181,
  #184, #185).

## Review output

```text
Summary: <what the PR does, verdict>

Blocking
- path/file.cpp:123 — <defect>. Scenario: <inputs/platform → failure>. Fix: <suggestion>.

Should fix
- …

Merge gates: [ ] GCC / MSVC / Clang  [ ] USD 24.11 + latest  [ ] tests & baselines justified
             [ ] downstream builds green
```
