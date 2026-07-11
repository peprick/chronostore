# C++ API Guide

ChronoStore exposes a small dependency-free public surface under
`include/chronostore`. Internal codecs, manifests, segments, locks, and WAL
types are intentionally not public.

## Minimal Example

```cpp
#include <chronostore/database.hpp>

#include <optional>

int main() {
    chronostore::Database database{"telemetry-db"};
    const chronostore::SeriesKey series{
        "temperature", {chronostore::Tag{"room", "lab"}}};

    database.put(
        series,
        chronostore::Sample{
            chronostore::Timestamp{1'700'000'000'000'000'000LL}, 21.5});

    const std::optional<chronostore::Sample> latest = database.latest(series);
    return latest.has_value() ? 0 : 1;
}
```

Link the CMake target `ChronoStore::chronostore` and compile as C++20 or newer.

## Data Model

### `Tag`

A tag has a non-empty string key and an arbitrary string value. Empty values
are valid. Tags compare lexicographically by key and then value.

### `SeriesKey`

A series key combines a non-empty measurement with tags. Construction sorts
tags by key and rejects duplicate keys, so tag input order never changes series
identity.

### `Timestamp`

Timestamps are signed 64-bit nanoseconds since the Unix epoch. Values before
the epoch are valid and no wall-clock conversion is performed by the library.

### `Sample`

A sample combines a timestamp with a finite IEEE 754 `double`. NaN and positive
or negative infinity are rejected.

## Opening A Database

```cpp
chronostore::DatabaseOptions options;
options.durability = chronostore::Durability::sync_on_write;
options.memtable_flush_threshold_samples = 4096;

chronostore::Database database{"telemetry-db", options};
```

The constructor creates missing directories, acquires an exclusive process
lock, validates the manifest and segments, repairs an incomplete WAL tail, and
replays complete records. Exactly one process may own a directory.

`Database` is move-only. Concurrent operations on one live instance are safe.
Operations on a moved-from object throw `std::logic_error`.

## Writes And Durability

```cpp
database.put(series, sample);
database.sync();
database.flush();
database.compact();
```

- `put` appends the mutation to the WAL before changing the MemTable.
- Writing the same `(series, timestamp)` replaces its value without increasing
  the logical count.
- `sync_on_write` synchronizes every successful WAL append before returning.
- `buffered` requires an explicit `sync` or `flush` for a caller-defined
  durability point.
- `flush` publishes the active MemTable as an immutable segment and resets the
  WAL.
- `compact` synchronously merges all current segments and publishes their
  replacement.

A zero `memtable_flush_threshold_samples` disables automatic flushes. Automatic
and manual flushes run on the calling thread.

## Queries

```cpp
const auto exact = database.get(series, timestamp);
const auto newest = database.latest(series);
const auto samples = database.range(series, start, end);
const auto known_series = database.series();
```

- `get` returns an exact timestamp match.
- `latest` returns the greatest timestamp for a series.
- `range` returns timestamp-ordered samples in the half-open interval
  `[start, end)` and rejects reversed bounds.
- `series` returns each known series once in canonical order.
- Missing point/latest values return `std::nullopt`; missing ranges return an
  empty vector.

Queries hold a shared engine lock through result construction. Readers may run
concurrently, while writes and maintenance operations wait for active readers.

## Statistics

`Database::stats()` returns all counters under one shared lock:

| Field | Meaning |
|---|---|
| `sample_count` | Unique logical `(series, timestamp)` pairs |
| `memtable_sample_count` | Logical samples in the active MemTable |
| `segment_count` | Immutable files in the current manifest |
| `wal_size_bytes` | Active WAL size in bytes |

`sample_count()` and `empty()` provide convenience access to the logical count.

## Errors

| Error | Meaning |
|---|---|
| `DatabaseBusyError` | Another process owns the directory |
| `DatabaseCorruptionError` | A WAL, manifest, segment envelope, index, or touched block is invalid |
| `std::invalid_argument` | Invalid model value, path, range, or option |
| `std::system_error` | Operating-system or filesystem operation failed |
| `std::logic_error` | An operation was called on a moved-from object |
| `std::runtime_error` | The engine entered an unusable state after a failed operation |

An engine that encounters an uncertain write or maintenance failure marks
itself unusable. Reopen the database before continuing.

## Version Information

```cpp
#include <chronostore/version.hpp>

static_assert(chronostore::version_major == 0);
auto version = chronostore::version_string;
```

Source API and persistent-format compatibility remain pre-1.0. Unknown file
versions and flags are rejected instead of guessed.
