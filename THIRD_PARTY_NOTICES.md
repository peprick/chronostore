# Third-Party Notices

ChronoStore does not vendor third-party source in this repository. Optional
dependencies are fetched by CMake at pinned release tags and remain governed by
their own licenses.

## Test Dependency

- [GoogleTest 1.17.0](https://github.com/google/googletest/tree/v1.17.0) is
  licensed under the BSD 3-Clause License.

GoogleTest is fetched only when `CHRONOSTORE_BUILD_TESTS=ON` and is not
installed with the ChronoStore package.

## ChronoView Dependencies

- [Dear ImGui 1.92.8](https://github.com/ocornut/imgui/tree/v1.92.8) is
  licensed under the MIT License.
- [ImPlot 1.0](https://github.com/epezent/implot/tree/v1.0) is licensed under
  the MIT License.
- [GLFW 3.4](https://github.com/glfw/glfw/tree/3.4) is licensed under the
  zlib/libpng License.

These projects are fetched only when `CHRONOSTORE_BUILD_GUI=ON`. Their license
and copyright files are available in the fetched source trees and upstream
repositories.

## ChronoStore License Status

No license has yet been selected for ChronoStore itself. The dependency
licenses above apply only to their respective projects and do not grant rights
to ChronoStore source code.
