# Changelog

All notable project changes are recorded here. ChronoStore follows semantic
versioning for source releases; persistent format compatibility remains
pre-1.0.

## Unreleased

### Added

- Cross-platform GitHub Actions CI for tests, sanitizers, package consumption,
  and ChronoView compilation.
- CMake presets for development, release, sanitizers, benchmarks, and GUI
  builds.
- Public version constants and CLI `--help`, `--version`, and `sync` commands.
- Getting-started, API, benchmark, roadmap, contribution, and security
  documentation plus structured issue and pull-request templates.
- MIT licensing for ChronoStore, including installed license and dependency
  notices.

### Changed

- Renamed `CHRONOSTORE_BUILD_TOOLS` to `CHRONOSTORE_BUILD_CLI`; the old option
  remains as a deprecated compatibility alias.
- Renamed public MemTable threshold/stat fields to include explicit sample
  units.
- Renamed the benchmark target and executable to `chronostore_benchmark` and
  `chronostore-benchmark`.

### Fixed

- The benchmark now refuses an existing scratch path instead of deleting a
  caller-supplied directory.
- Generated `imgui.ini` state is no longer tracked.

## 0.1.0 - 2026-07-09

### Added

- Canonical time-series model with validated tags, nanosecond timestamps, and
  finite numeric samples.
- Ordered MemTable with overwrite, point, latest, range, snapshot, and series
  discovery operations.
- Bounds-checked little-endian codec and portable CRC32C implementation.
- Versioned checksummed WAL records, cross-platform file writer, incomplete
  tail repair, and deterministic recovery.
- Immutable checksummed segment blocks and files with sparse block indexes and
  on-demand reads.
- Atomic manifest publication, MemTable flush, durable WAL reset, orphan
  cleanup, crash-window de-duplication, and whole-segment compaction.
- Thread-safe public `Database` API, configurable durability and flush
  threshold, stable public corruption/busy errors, and engine statistics.
- Exclusive cross-platform database-directory locking.
- `chronostore` CLI for writes, queries, series listing, statistics, flush, and
  compaction.
- Native optional ChronoView GUI with series discovery, plotting, writes,
  maintenance, stats, and deterministic demo data.
- CMake install/export package, external example, benchmark driver, strict
  warnings, sanitizer option, and 112 automated tests.
