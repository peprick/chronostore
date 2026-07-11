# Getting Started

ChronoStore can be used as an embedded C++ library, through its CLI, or through
the optional ChronoView desktop inspector.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler: Clang, AppleClang, GCC, or MSVC
- Ninja for the included CMake presets
- Git and network access for the first test or ChronoView configuration
- clang-format for contribution formatting checks
- OpenGL and platform window-development libraries for ChronoView

The core library itself depends only on the C++ standard library and threads.

## Development Build

```bash
git clone https://github.com/peprick/chronostore.git
cd chronostore
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

The first development configuration fetches the pinned GoogleTest release.

## Available Presets

| Preset | Purpose |
|---|---|
| `dev` | Debug library, CLI, examples, and tests |
| `release` | Optimized library and CLI without development dependencies |
| `sanitizers` | Debug tests with AddressSanitizer and UndefinedBehaviorSanitizer |
| `benchmark` | Optimized benchmark driver |
| `gui` | Optimized CLI and ChronoView desktop application |

Each configure preset has a build preset with the same name. The `dev` and
`sanitizers` presets also have matching test presets.

## Manual Configuration

Presets are optional. A direct CMake configuration works with any supported
generator:

```bash
cmake -S . -B build-manual -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHRONOSTORE_BUILD_CLI=ON \
  -DCHRONOSTORE_BUILD_TESTS=OFF
cmake --build build-manual --parallel
```

### CMake Options

| Option | Top-level default | Purpose |
|---|---:|---|
| `CHRONOSTORE_BUILD_CLI` | `ON` | Build the `chronostore` command-line client |
| `CHRONOSTORE_BUILD_TESTS` | `ON` | Build the GoogleTest suite |
| `CHRONOSTORE_BUILD_EXAMPLES` | `ON` | Build API examples |
| `CHRONOSTORE_BUILD_BENCHMARKS` | `OFF` | Build `chronostore-benchmark` |
| `CHRONOSTORE_BUILD_GUI` | `OFF` | Build the ChronoView inspector |
| `CHRONOSTORE_ENABLE_SANITIZERS` | `OFF` | Enable ASan and UBSan with Clang or GCC |

CLI, tests, and examples default to `OFF` when ChronoStore is included as a
subproject. `CHRONOSTORE_BUILD_TOOLS` remains a deprecated compatibility alias
for `CHRONOSTORE_BUILD_CLI`.

## CLI Walkthrough

The development preset writes executables under `build/dev`.

```bash
export DB=./demo-db

./build/dev/chronostore put "$DB" temperature 100 21.5 room=lab
./build/dev/chronostore put "$DB" temperature 200 22.75 room=lab
./build/dev/chronostore get "$DB" temperature 100 room=lab
./build/dev/chronostore latest "$DB" temperature room=lab
./build/dev/chronostore range "$DB" temperature 0 1000 room=lab
./build/dev/chronostore series "$DB"
./build/dev/chronostore stats "$DB"
./build/dev/chronostore flush "$DB"
./build/dev/chronostore compact "$DB"
```

Use `chronostore --help` for the command list and `chronostore --version` for
the linked release. Successful commands return `0`, operational or input
errors return `1`, and a missing point/latest result returns `2`.

## ChronoView

```bash
cmake --preset gui
cmake --build --preset gui --parallel
./build/gui/chronoview ./demo-db --demo
```

The `--demo` flag writes and plots a deterministic temperature series. Only
one process can own a database directory, so close the CLI operation or
ChronoView database before opening the same path elsewhere.

## Install The Package

```bash
cmake --preset release
cmake --build --preset release --parallel
cmake --install build/release --prefix "$PWD/install"
```

Consume the exported target from another CMake project:

```cmake
find_package(ChronoStore 0.1 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE ChronoStore::chronostore)
```

Configure that project with `-DCMAKE_PREFIX_PATH=/path/to/chronostore/install`.
The installation contains the static library, public headers, CMake package
metadata, and the CLI when enabled.

## Common Problems

### Database Is Already Open

`DatabaseBusyError` and the equivalent CLI message mean another process owns
the directory. Close that process; do not delete `LOCK` while it is running.

### Dependency Fetch Fails

Tests and ChronoView fetch pinned sources on first configuration. Verify GitHub
network access or populate CMake's `FETCHCONTENT_SOURCE_DIR_<NAME>` override
with an existing source checkout.

### ChronoView Cannot Find OpenGL

Install the platform's OpenGL and window-system development packages, or build
the core and CLI with `CHRONOSTORE_BUILD_GUI=OFF`.
