# ChronoView

ChronoView is the optional native inspector for ChronoStore. It links the
public `ChronoStore::chronostore` target and does not read internal files or
call private storage classes.

![ChronoView range query](images/chronoview.png)

## Build

```bash
cmake --preset gui
cmake --build --preset gui --parallel
```

The first configuration fetches pinned source releases:

- [Dear ImGui 1.92.8](https://github.com/ocornut/imgui/releases/tag/v1.92.8)
- [ImPlot 1.0](https://github.com/epezent/implot/releases/tag/v1.0)
- [GLFW 3.4](https://github.com/glfw/glfw/releases/tag/3.4)

The GUI target also uses the platform OpenGL library. It is disabled by
default, so none of these dependencies affect a normal library build.

## Run

Open or create a database directory:

```bash
./build/gui/chronoview ./telemetry-db
```

Open a database and populate a deterministic 240-sample series for a quick
demonstration:

```bash
./build/gui/chronoview ./telemetry-db --demo
```

Launching without a path opens the window without acquiring a database. Enter
a directory in the left panel and select **Open**.

## Views

### Explore

- Select a discovered series from the left panel or enter a measurement and
  comma-separated tags.
- Run exact timestamp, latest-value, or half-open range queries.
- Range results are plotted with nanoseconds relative to the first result to
  avoid losing visible precision when converting nanosecond timestamps to
  plotting `double` values.
- The result table retains and displays each exact signed 64-bit timestamp.

### Write

- Enter a measurement, optional `key=value` tags, timestamp, and finite value.
- **Put** uses the database's configured durability mode.
- **Generate demo** writes, flushes, and immediately plots the deterministic
  temperature series used in the project screenshot.

### Maintenance

- **Sync WAL** explicitly synchronizes buffered WAL writes.
- **Flush** publishes the current MemTable as a segment and resets the WAL.
- **Compact** merges live segments and atomically publishes their replacement.

The left panel shows logical samples, current MemTable samples, live segment
count, WAL bytes, and all discovered series.

## Ownership

ChronoStore permits one owning process per database directory. ChronoView keeps
that lock while the database is open. Close the database in ChronoView before
using the CLI or another process on the same directory. Multiple threads
inside one owning process remain supported by the library.
