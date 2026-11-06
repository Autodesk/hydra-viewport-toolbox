# Testing HVT

How to run, filter, and author HVT tests. The Tests-vs-How-tos distinction lives in
[../AGENTS.md](../AGENTS.md); build commands and presets are in [build.md](build.md).

## Run tests

**Windows / Linux (match CI GPU jobs):** headless Vulkan — use the `debugwithvulkan` preset
(not available on macOS):

```bash
cmake --workflow --preset debugwithvulkan
# or: cmake --preset debugwithvulkan && cmake --build --preset debugwithvulkan
ctest --preset debugwithvulkan
```

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

Tests need a **Vulkan-capable GPU** on Windows, Linux, and Android when the build has
`HVT_ENABLE_VULKAN=ON` (for example `debugwithvulkan` or CI’s configure override). The harness uses
a **headless** `VulkanRendererContext`: no SDL window, surface, or swapchain — color is read back
through Hgi and compared to PNG baselines. A display server is not required on those platforms.
With `HVT_ENABLE_VULKAN=OFF` (`debug` / `release` presets), tests use **OpenGL via SDL2** and
need a working GPU/display setup as before.

| Platform | Default backend (`GetRenderingBackend()`) | Display / window |
|----------|-------------------------------------------|------------------|
| Windows, Linux (Vulkan on) | HgiVulkan | Headless (offscreen) |
| Windows, Linux (Vulkan off) | OpenGL via SDL2 | GPU + display (shared GL context optional) |
| macOS / iOS | Metal | GPU; Metal device required |
| Android | Vulkan | Platform-specific (see `AndroidTestContext`) |

Configure with `-DHVT_ENABLE_VULKAN=OFF` (or use macOS, where Vulkan is forced off) to run tests
on OpenGL instead. **Vulkan is not supported on macOS/iOS** — see [build.md](build.md).

In sandboxes with no GPU at all, rendering tests fail to initialize — use a build-only check
(`cmake --build --preset debug`).

### Environment variables

Both sharing optimizations are on by default and can be disabled when debugging a test that is
sensitive to residual GPU state (shared objects survive between tests):

| Variable | Default | Controls |
|----------|---------|----------|
| `HVT_TEST_SHARE_HGI` | `1` | Reuse one Hgi instance per backend (Vulkan, Metal) across tests instead of creating one per test context |
| `HVT_TEST_SHARE_GL_CONTEXT` | `1` | Borrow the process-wide OpenGL window and context for each test context instead of creating a window per test (OpenGL backend only) |

Computed PNG names from `HVT_TEST` include the backend token (e.g. `_Vulkan`, `_Metal`); golden
baselines under `test/data/baselines/` use **platform** suffixes (`_osx`, `_android`, …) via
`HydraRendererContext::getFilename`, not per-backend files. Image compares may use looser thresholds
when `#if defined(ENABLE_VULKAN)` allows small Vulkan vs OpenGL differences on the same platform.

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
- **Apple/Metal:** some tests skip `primId`-based rendering (non-deterministic) — see [outline.md](outline.md)
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
