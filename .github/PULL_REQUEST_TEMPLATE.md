## Summary

Describe the user-visible behavior and why the change is needed.

## Correctness

List any affected durability, recovery, concurrency, query, or file-format
invariants. Write `None` when the change does not touch them.

## Verification

- [ ] Focused tests cover the changed behavior.
- [ ] `cmake --build --preset dev` succeeds.
- [ ] `ctest --preset dev` passes.
- [ ] `clang-format --dry-run --Werror` passes for changed C++ files.
- [ ] Documentation and changelog entries are updated when needed.
- [ ] No generated build, database, IDE, or GUI state files are included.

## Compatibility

Call out public API, CLI output, CMake option, or persistent-format changes.
