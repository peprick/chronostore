# Benchmarking

ChronoStore includes a small reproducible workload driver. It is intended for
regression baselines and design comparisons, not universal performance claims.

## Build And Run

```bash
cmake --preset benchmark
cmake --build --preset benchmark --parallel
./build/benchmark/chronostore-benchmark 100000
```

The optional first argument is a positive sample count no greater than
`INT64_MAX`. The optional second argument is a scratch database directory:

```bash
./build/benchmark/chronostore-benchmark 100000 /tmp/chronostore-run-01
```

For safety, the driver refuses an existing path. It creates the scratch
database and removes it when the process exits normally or through a handled
exception.

## Workload

The current driver:

1. Opens one series tagged `source=sequential`.
2. Writes sequential integer nanosecond timestamps and deterministic values.
3. Uses buffered WAL durability and an 8192-sample flush threshold.
4. Flushes all remaining samples.
5. Executes 1000 deterministic half-open range queries of up to 100 samples.
6. Reports elapsed write/query time, throughput, samples read, and segment
   count.

The write result includes segment encoding and flush time but not per-write
durable synchronization. It must not be presented as sync-on-write throughput.

## Reporting Results

Include enough context to reproduce a number:

- ChronoStore commit and build type;
- compiler and version;
- operating system, architecture, CPU, memory, and storage;
- sample count and exact command;
- durability and flush threshold;
- whether the run was cold or repeated;
- median and dispersion across multiple runs.

Treat a single run as a smoke result. Compare changes on the same machine with
the same workload before drawing conclusions.

## Planned Improvements

- latency percentiles rather than aggregate throughput only;
- separate WAL, flush, indexed-read, and compaction workloads;
- configurable series cardinality and timestamp distributions;
- storage-size and peak-memory reporting;
- machine-readable JSON output;
- automated before/after regression comparison.
