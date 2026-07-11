# Roadmap

ChronoStore develops storage behavior in dependency order: correctness and
recovery first, then measured performance features. Items are priorities, not
date commitments.

## Shipped In 0.1

- canonical series and finite numeric samples;
- ordered MemTable and last-write-wins semantics;
- checksummed WAL with repairable incomplete tails;
- immutable segment blocks and sparse indexes;
- atomic manifests, flush, recovery, and whole-segment compaction;
- thread-safe public API and exclusive process ownership;
- CLI, installable CMake package, benchmark driver, and ChronoView;
- corruption, truncation, crash-window, concurrency, CLI, and package tests.

## Near-Term Priorities

### Measurement And Failure Injection

- publish latency distributions for WAL, flush, query, and compaction paths;
- add controlled process-termination tests around each publication boundary;
- report peak memory and storage amplification;
- add randomized model-based operation sequences.

### Compression

- timestamp delta and delta-of-delta encodings;
- XOR encoding for double-precision values;
- versioned block flags with strict fallback behavior;
- compression-ratio, encode-cost, and decode-cost reporting.

### Query Efficiency

- bounded decoded-block cache with hit, miss, memory, and eviction counters;
- streaming range cursor that does not materialize the entire result;
- lower-allocation multi-source merge path.

### Background Maintenance

- immutable active/frozen MemTable handoff;
- background flush worker;
- leveled compaction selection;
- safe old-segment reclamation after readers release snapshots.

## Later Work

- deletes, tombstones, and retention policies;
- stable file-format migration tooling;
- broader benchmark workloads and continuous regression tracking;
- optional Python bindings or an HTTP adapter above the C++ API;
- richer ChronoView diagnostics for file layout and compaction activity.

## Non-Goals

ChronoStore is not trying to become a SQL database, distributed consensus
system, multi-tenant cloud service, or general-purpose document store. New
features should preserve the value of a small embedded engine whose persistent
behavior can be understood and tested end to end.
