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


## vcpkg Integration
This project uses vcpkg (via manifest mode) to manage third-party dependencies in a self-contained and clean way.

🧰 Setup Instructions

To ensure dependencies are correctly installed:
1.	Clone with submodules:
```bash
git clone --recurse-submodules https://github.com/autodesk/hydra-viewport-toolbox.git
```
2.	Build with CMake:
No manual setup needed! If vcpkg is not yet bootstrapped, the build will automatically do it for you:

If building as part of a larger CMake project, you can still override CMAKE_TOOLCHAIN_FILE externally if needed.
•	If the vcpkg/ submodule is missing, a warning will be printed.

💡 Notes
•	The vcpkg toolchain is only set if:
    • VCPKG_MANIFEST_FEATURES is defined
    • CMAKE_TOOLCHAIN_FILE is not already set
    • The vcpkg/ directory exists (fetched via Git submodules)
•	This ensures clean integration for both:
•	Standalone builds of this project
•	Parent projects that include this project as a submodule