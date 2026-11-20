# Testing HVT

How to run, filter, and author HVT tests. The Tests-vs-How-tos distinction lives in
[../AGENTS.md](../AGENTS.md); build commands and presets are in [build.md](build.md).

## Run tests

**Match CI GPU jobs:** headless Vulkan — use the `debugwithvulkan` preset, whose test preset
selects the Vulkan backend (on macOS, a Vulkan driver such as MoltenVK or Lavapipe is needed):

```bash
cmake --workflow --preset debugwithvulkan
# or: cmake --preset debugwithvulkan && cmake --build --preset debugwithvulkan
ctest --preset debugwithvulkan
```

For an optimized build use `relwithvulkan` the same way.

**OpenGL backend (SDL window):** the `debug` preset sets `HVT_ENABLE_VULKAN=OFF` via the shared
`base` preset. Use it when debugging OpenGL-specific behavior or when you do not need Vulkan:

```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug
```

All tests compile into one `hvt_test` binary at `build/<preset>/bin/hvt_test`. The preset name
must match between configure, build, and test (`debug` vs `debugwithvulkan` are separate trees).

### Fast iteration (single tests)

Prefer a targeted subset over the full suite while iterating:

```bash
# Discover test names (suite.name)
./build/debug/bin/hvt_test --gtest_list_tests

# Run one test or a suite. gtest_filter matches "Suite.Name" and treats '.' literally, so the
# prefix before '.*' must be the exact suite name (e.g. TestOutlineTasks, not OutlineTask) —
# a wrong suite name runs 0 tests and still exits 0. Use --gtest_list_tests to confirm names.
./build/debug/bin/hvt_test --gtest_filter=howTo.createOneFramePass
./build/debug/bin/hvt_test --gtest_filter='TestOutlineTasks.*'

# Same filtering through ctest by regex (substring match on the test name)
ctest --preset debug -R howTo
ctest --preset debug -R TestOutlineTasks --output-on-failure
```

### Selecting the backend

The backend is selected at **runtime**, once per process, by precedence:

1. the `--hgi=<OpenGL|Metal|Vulkan>` command-line flag (case-insensitive),
2. the `HVT_TEST_HGI` environment variable,
3. the OpenUSD platform default (`Hgi::CreatePlatformDefaultHgi()`): Metal on macOS, OpenGL
   elsewhere, or Vulkan when `HGI_ENABLE_VULKAN` is set.

```bash
./build/debugwithvulkan/bin/hvt_test --hgi=Vulkan
HVT_TEST_HGI=Metal ctest --preset debugwithvulkan
```

The Vulkan backend needs a build with `HVT_ENABLE_VULKAN=ON` (OpenUSD built with HgiVulkan, e.g.,
the `debugwithvulkan` preset); its test presets and the CI set `HVT_TEST_HGI=Vulkan`. Selecting a
backend that the build or the machine does not provide fails each rendering test with an error.

| Backend | Test context | Display / window |
|---------|--------------|------------------|
| Vulkan | `VulkanRendererContext` | Headless (offscreen): color is read back through Hgi |
| OpenGL | `OpenGLRendererContext` | SDL2 window and OpenGL context |
| Metal | `OpenGLRendererContext` | SDL2 window, presenting through OpenGL interop |

On macOS, Vulkan needs a Vulkan driver such as MoltenVK or Mesa's Lavapipe (e.g., set
`VK_DRIVER_FILES` to its ICD file); HVT does not provide one.

When the Vulkan backend is selected and `HGI_ENABLE_VULKAN` is unset, `hvt_test` restarts itself
with it (OpenUSD reads its environment settings before `main()`), so `ctest` and direct runs behave
the same. Downstream test executables should set it before starting the process. Without it, OpenUSD's Storm support check
probes the platform default Hgi (OpenGL), which fails on a headless host, so Storm is created
without its scene-index plugins and implicit surfaces (cubes, spheres) render black. An explicit
environment value always wins.

In sandboxes with no GPU at all, rendering tests fail to initialize — use a build-only check
(`cmake --build --preset debug`).

### Validating the branch on every preset

`.claude/skills/validate-branch/validate-branch.sh` configures, builds and tests the branch on every
configure preset CMake lists on the host. It runs
`hvt_test` directly instead of `ctest`, and applies the `ASAN_OPTIONS` and `UBSAN_OPTIONS` that the
presets define, which only `cmake`/`ctest --preset` apply otherwise. It runs from bash on macOS,
Linux and Windows (Git Bash). See the skill's `SKILL.md` for the options.

### Environment variables

Both sharing optimizations are on by default and can be disabled when debugging a test that is
sensitive to residual GPU state (shared objects survive between tests):

| Variable | Default | Controls |
|----------|---------|----------|
| `HVT_TEST_SHARE_HGI` | `1` | Reuse one Hgi instance per backend (Vulkan, Metal) across tests instead of creating one per test context |
| `HVT_TEST_SHARE_GL_CONTEXT` | `1` | Borrow the process-wide OpenGL window and context for each test context instead of creating a window per test (OpenGL backend only) |

| `HVT_TEST_HGI` | platform default | The backend the tests render with (see above) |
| `HVT_IDIFF` | found at configure time | The OpenImageIO `idiff` executable that compares the images (OpenImageIO 3.1.9 or later) |
| `HVT_TEST_RUN_SKIPPED` | `0` | Runs the tests that `HVT_TEST_SKIP_IF` / `HVT_TEST_SKIP_BACKEND` would skip, logging the reason instead (the CI sets it) |

### Baselines

Computed PNG names from `HVT_TEST` include the backend token (e.g., `_Vulkan`, `_Metal`). A
baseline is looked up by its logical name under `test/data/baselines/`, and the most precise
variant wins (see `ResolveBaselinePath`):

1. `<name>_<OS>_<API>.png`: this OS and this API,
2. `<name>_<API>_<OS>.png`: this API on this OS,
3. `<name>_<OS>.png`: every API on this OS,
4. `<name>_<API>.png`: this API on every OS,
5. `<name>.png`: everything.

`<OS>` is `osx`, `win` or `linux`, and `<API>` is the Hgi token (`OpenGL`, `Metal` or `Vulkan`).
One image is normally right everywhere, so add a suffixed baseline only for a test that genuinely
renders differently. Prefer the API suffix: the OS one is an imperfect proxy for the hardware.

### Image comparison

Images are compared with OpenImageIO's
[idiff](https://openimageio.readthedocs.io/en/latest/idiff.html): a pixel fails when a channel
differs by more than the threshold (in 8-bit units), and the comparison fails when more pixels
than the pixel count threshold fail. On failure, an amplified difference image is written next to
the computed image with a `_diff` suffix. `idiff` comes from the external OpenUSD install when
`OPENUSD_INSTALL_PATH` is set, otherwise from the vcpkg `image-compare` feature (OpenImageIO
`tools`). It must be from OpenImageIO 3.1.9 or later, which configure checks.

A comparison can instead be perceptual (the `perceptual` argument of `validateImages` and the
`compare*` functions, off by default): a pixel then fails only when the difference would be
visible to a viewer (Yee's metric, on the luminance and the color, ignoring alpha), so
differences hidden by high-contrast surroundings pass and the per-channel threshold is ignored.
It suits images where only the visible result matters, not data (ids, depth) or thin features
(outlines).

### Skipping tests

Skip a test at runtime, at the top of its body, rather than renaming it `DISABLED_`, so the skip
shows up in the report:

- `HVT_TEST_SKIP_BACKEND(pxr::HgiTokens->Metal);` for a graphics API issue,
- `HVT_TEST_SKIP_IF(TestHelpers::kIsLinux, "reason");` for an operating-system issue.

Both honor `HVT_TEST_RUN_SKIPPED=1`, which runs the test anyway to show the current state of the
known issues.

Do not use the platform as a proxy for the graphics API (e.g., `__APPLE__` for Metal): query
`TestHelpers::GetRenderingBackend()` or `Hgi::GetAPIName()`.

## Test layout

| Path | Purpose |
|------|---------|
| `test/tests/` | Unit and image-comparison tests — primary validation suite |
| `test/howTos/` | Usage demonstrations (also executed as tests) |
| `test/data/baselines/` | Golden PNG images for rendered output |
| `test/data/assets/` | USD scenes and other test assets |
| `test/RenderingFramework/` | Shared test utilities (image compare, traces) |

## Image comparison tests

- Compare rendered output against baselines in `test/data/baselines/`
- `primId`-based rendering is deterministic on every backend except Vulkan; prefer one baseline for every backend, and add a suffixed one (e.g., `_Metal`) only when a higher threshold cannot absorb a genuine difference
- When adding a new rendered test, add a baseline PNG and document platform skips if needed
- **Multi-pass color compositing:** when a later pass shares the color AOV but not depth (overlay
  inset with its own camera), set `clearBackgroundDepth = true` on that pass so overlay geometry
  depth-tests correctly on Vulkan; call `waitForGPUIdle()` after the overlay render before readback.
  See [framepass.md](framepass.md) and `test/howTos/howTo03_CreateTwoFramePasses.cpp`.
- **GpuImageCapture:** for `clearBackgroundColor == false` and `visualizeAOV == color`, PNG
  readback uses the shared `HdRenderBuffer`, not the last task texture — see
  `test/RenderingFramework/GpuImageCapture.cpp`. The PNG has the dimensions of the captured
  texture, so a test that resizes the render buffers saves an image of that size, not of the window.

## Adding coverage for new features

**Unit tests** (`test/tests/`) — required for validation:

1. Params equality / default tests (mirrors existing `*ParamsEquality` tests)
2. Construction / teardown via `TaskManager`
3. Rendered image test with baseline (where applicable)
4. Additional cases for edge behavior and regressions as needed

**How-to** (`test/howTos/`) — one integration example when the feature is user-facing:

1. Show the minimal end-to-end usage an application would follow
2. Document in [../test/README.md](../test/README.md) if the feature is listed there

## Tracing

See [howToCollectTraces.md](howToCollectTraces.md) — use `RenderingUtils::CollectTraces` with
`PXR_ENABLE_GLOBAL_TRACE=1`.
