# Hydra Viewport Toolbox
The **Hydra Viewport Toolbox** (HVT) is a library of utilities that can be used by an application to simplify the use of [OpenUSD](https://openusd.org) Hydra for the application's graphics viewports. The utilities can be used together or independently to add common viewport functionality and improve the performance and visual quality of viewports.

HVT currently includes the following features but it is being expanded to include even more.

- Layering of Hydra render delegate output, optionally from different render delegates ("passes").
- Management of multiple viewports.
- Hydra task management, supporting application-defined lists of tasks.
- Management of data commonly needed for tasks, e.g. render buffers and lighting.
- Tasks for features commonly needed for viewports, e.g. antialiasing and ambient occlusion.
- User interaction for common operations, e.g. selection and camera manipulation.

HVT is developed and maintained by Autodesk. The contents of this repository are fully open source under [the Apache license](LICENSE.md), with [feature requests and code contributions](CONTRIBUTING.md) welcome!

## 🚀 Quick Start

To build the project locally using the default configuration (Linux/macOS/Windows), run:
```bash
cmake --preset debug
cmake --build --preset debug
```
This uses the built-in vcpkg manifest and cmake presets to automatically configure dependencies and build paths. No manual setup is needed.

For more information or to customize the configuration, see [Using CMake Presets](#using-cmake-presets) and [vcpkg Integration](#vcpkg-integration).

## ✅ Continuous Integration (CI)

CI builds and tests are run via GitHub Actions using shared CMake presets.

### 🧪 CI Minimal

A lightweight CI workflow (ci-minimal.yaml) runs automatically on every pull request and push. It builds and tests on Linux (Debug) to validate basic correctness without consuming too many build resources.

### 🔁 CI Full

A full matrix workflow (ci-full.yaml) tests on Linux, macOS, and Windows with both Debug and Release configurations. This does not run by default on every PR.
To run it manually, use the “Run workflow” button under the “Actions” tab on GitHub after pushing your branch.
It also runs when a PR merges into main.

### 🧱 Reusable Build Steps

Common build logic is centralized in .github/workflows/ci-steps.yaml. It defines a reusable workflow that takes OS and build type as inputs. Both minimal and full CI workflows delegate to this reusable workflow.

## vcpkg Integration

This project uses vcpkg in manifest mode to manage third-party dependencies cleanly and automatically.

🧰 Zero-Setup Build

All required vcpkg steps (initialization, bootstrapping, and toolchain setup) are fully automated. You do not need to install vcpkg manually or set any variables—just configure and build using one of the provided CMake presets (see below).
If the externals/vcpkg/ submodule is missing or uninitialized, the build system will fetch and bootstrap it for you.

💡 How it Works
  •	The logic is handled in cmake/VcpkgSetup.cmake.
  •	vcpkg is only enabled if: The tests are enabled or no USD installation path provided
  •	Then the vcpkg submodule will be fetched, vcpkg will be bootstrapped, toolchain will be set.
  •	This ensures seamless support for both standalone builds and builds as part of larger projects.

📦 USD Integration
  •	If OPENUSD_INSTALL_PATH is not set, the vcpkg usd-minimal feature is enabled by default.
  •	You can override this to use a local OpenUSD install by setting OPENUSD_INSTALL_PATH from env or cmake.

🔁 Customizing the vcpkg Triplet (Optional)
By default, the vcpkg triplet is inferred from the target platform (Located in externals/vcpkg/triplets).
You can override this by setting the following environment variable before configuration:
```bash
export HVT_BASE_TRIPLET_FILE=/absolute/path/to/custom-triplet.cmake
```
This allows advanced users to:
  •	Customize compiler flags or features used for all dependencies
  •	Switch between shared vs static libraries
  •	Target custom platforms or ABIs
Make sure your custom triplet inherits from a standard vcpkg triplet if needed.

## Using CMake Presets

This project uses CMake Presets to define consistent and shareable build configurations across local development and CI.

### 🧰 Why Presets?
Presets provide a clean, declarative way to manage build options, toolchain setup, and environment variables. In this project, they are used to:
	•	Simplify getting started with local builds.
	•	Automatically configure vcpkg when needed.
	•	Keep CI scripts clean by reusing the same preset logic used locally.

All CI builds use these same presets internally, ensuring consistent behavior between local and automated builds.

### 🔧 Building with CMake Presets

This project provides CMakePresets.json-based builds for easy setup. Presets ensure consistent options and automatic toolchain configuration.

To configure and build:
```bash
cmake --preset debug       # Configure Debug build with vcpkg
cmake --build --preset debug
```

Or to use a specific OpenUSD install:
```bash
# Requires setting OPENUSD_INSTALL_PATH env to be set before calling cmake
export OPENUSD_INSTALL_PATH=/path/to/local/usd
cmake --preset debug
cmake --build --preset debug
```

### 🧪 Running Tests with CMake Presets
Use the test preset to run the test suite:
```bash
ctest --preset debug
```

### 🛠️ Custom Builds with CMake User Presets

You can create your own build configurations by adding a CMakeUserPresets.json file in the project root. This file is excluded by .gitignore, so it won’t interfere with version control or shared presets.

This is ideal for:
  •	Specifying custom paths (e.g., OPENUSD_INSTALL_PATH)
  •	Changing build directories
  •	Tweaking options like enabling experimental features or using alternate compilers

📁 Example CMakeUserPresets.json:
```bash
{
  "version": 3,
  "configurePresets": [
    {
      "name": "my-debug",
      "inherits": "debug",
      "environment": {
      },
      "cacheVariables": {
        "OPENUSD_INSTALL_PATH": "/path/to/my/openusd"
      }
    }
  ]
}
```

Use it just like the built-in presets:
```bash
cmake --preset my-debug
cmake --build --preset my-debug
```

💡 For more info on user presets, see the CMake Presets documentation.