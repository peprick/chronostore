# Contributing To ChronoStore

Thanks for taking the time to improve ChronoStore. Contributions should keep
the storage engine small, inspectable, and explicit about correctness.

## Before Starting

- Search existing issues and the [roadmap](docs/roadmap.md).
- Open an issue before large API, format, concurrency, or dependency changes.
- Never include production databases, credentials, or private telemetry in a
  reproduction.

## Development Setup

Install a C++20 compiler, CMake 3.24 or newer, Ninja, Git, and clang-format.
Then use the development preset:

```bash
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

The first configuration fetches the pinned GoogleTest source. Sanitizer and
GUI builds have dedicated presets:

```bash
cmake --preset sanitizers
cmake --build --preset sanitizers --parallel
ctest --preset sanitizers

cmake --preset gui
cmake --build --preset gui --parallel
```

## Engineering Guidelines

- Preserve WAL-before-MemTable mutation ordering.
- Treat manifest replacement as the persistent segment-set commit point.
- Validate lengths, offsets, counts, and checksums before allocation or
  interpretation.
- Never persist native object layouts, pointers, padding, or `std::hash`
  values.
- Keep last-write-wins and half-open range semantics consistent across memory,
  recovery, segments, and compaction.
- Prefer standard-library and existing project patterns over new dependencies.
- Add abstractions only when they remove demonstrated complexity.
- Keep public headers free of internal and third-party implementation types.

See [Architecture](docs/architecture.md) and
[File Formats](docs/file-formats.md) before changing persistent behavior.

## Style

C++ files use the repository's `.clang-format` configuration and must remain
warning-clean under the CMake warning policy.

```bash
find include src tests tools examples -type f \
  \( -name '*.cpp' -o -name '*.hpp' -o -name '*.hpp.in' \) \
  -exec clang-format --dry-run --Werror {} +
```

Use concise comments for invariants and non-obvious decisions. Public APIs
should document ownership, units, boundaries, exceptions, and thread-safety
semantics.

## Tests

Scale tests with the risk of the change. Storage-format and recovery changes
should include malformed input, truncation, checksum, restart, and crash-window
coverage where applicable. Concurrency changes should include deterministic
multi-threaded coverage rather than timing-only assertions.

## Pull Requests

Keep pull requests focused. Explain the behavioral change, affected invariants,
compatibility impact, and exact verification performed. Do not commit build
directories, generated databases, `imgui.ini`, or editor state.
