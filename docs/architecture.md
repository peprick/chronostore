# ChronoStore Architecture

This document captures the intended architecture before implementation. It is
a design baseline, not a claim that the listed components already exist.

## Scope

ChronoStore is a single-node, embeddable time-series storage engine. Its core
workload is a high rate of timestamped numeric writes followed by point,
latest-value, and bounded time-range queries.

The first complete version will prioritize:

- correct recovery after abrupt process termination;
- deterministic on-disk formats;
- bounded memory usage during reads;
- concurrent readers with an explicitly serialized write order;
- transparent observability and reproducible performance measurements.

SQL, joins, distributed replication, authentication, and multi-tenant resource
isolation are outside the initial scope.

## Data Model

A logical series consists of:

- a non-empty measurement name;
- a canonical, uniquely keyed collection of string tags.

A sample consists of:

- a signed 64-bit Unix timestamp in nanoseconds;
- a finite IEEE 754 double-precision value.

Tags are stored in deterministic key order so that input order does not change
series identity. Persistent numeric series IDs will be assigned by a catalog.
Implementation-defined values such as `std::hash` results will never be used as
durable identifiers.

## Core Components

### Public API

The library owns database lifecycle, validates writes, exposes range queries,
and reports operational errors without exposing file-format details. Public
types should have clear ownership and lifetime rules and should avoid leaking
third-party dependencies into client code.

### Series Catalog

The catalog maps canonical series keys to stable numeric IDs. Numeric IDs keep
repeated strings out of sample blocks and make indexes smaller. Catalog changes
must follow the same recovery guarantees as sample writes.

### Write-Ahead Log

The write-ahead log records accepted mutations before they are acknowledged.
Records are length-delimited, versioned, and checksummed. Recovery replays the
valid prefix and treats a trailing incomplete record as an interrupted write.

The exact durability contract, including when an `fsync`-equivalent operation
occurs, will be configurable and documented before the WAL is implemented.

### In-Memory Table

The active memory table organizes samples by series and timestamp. Once it
reaches a configured memory threshold, it becomes immutable and a new active
table accepts writes. Immutable tables can be queried while a background worker
flushes them to disk.

The first implementation will prefer standard-library containers. Specialized
allocators or probabilistic structures require profiling evidence before being
introduced.

### Segment Files

Flushed samples are written to immutable, versioned segment files. A segment is
published only after its contents and metadata are complete. Expected regions
include:

1. format header;
2. series and block metadata;
3. timestamp/value blocks;
4. sparse indexes;
5. checksummed footer and offsets.

Unknown format versions must be rejected safely. Readers must validate lengths
and offsets before allocating memory or seeking within a file.

### Query Engine

A query identifies a series and a half-open time interval. The engine finds
overlapping sources, seeks through sparse indexes, and merges ordered iterators
from memory and segments. Results are streamed so query memory depends on the
number of sources and block size rather than the total result size.

Duplicate-timestamp and overwrite semantics will be specified before writes are
persisted because those rules affect recovery, merging, and compaction.

### Manifest

The manifest identifies the set of segment files that constitute a valid
database state. Updates use write-new, synchronize, and atomic-replacement
semantics. Startup must choose one complete state and never infer validity from
partially written segment files.

### Compaction

Compaction merges selected immutable segments, applies overwrite or deletion
rules, writes replacement segments, and atomically publishes a new manifest.
Old files remain available until active readers can no longer reference them.

### Block Cache

A bounded cache may retain decoded or compressed blocks used by recent queries.
It will expose hit rate, memory use, and eviction statistics. Eviction policy
and synchronization will be selected from benchmark evidence.

## Concurrency Model

The initial model is:

- multiple calling threads may submit writes;
- one write coordinator establishes WAL and mutation order;
- multiple readers may execute concurrently;
- immutable tables and segments require no mutation locks;
- background flush and compaction publish new state atomically.

The API will state whether a query observes a snapshot taken at query start and
how newly acknowledged writes become visible. Those semantics must be tested,
not inferred from lock placement.

## Recovery Model

Startup recovery will:

1. load and validate the latest complete manifest;
2. open the segment files referenced by that manifest;
3. find the WAL position represented by those segments;
4. replay each complete, checksummed WAL record after that position;
5. restore the active memory table and resume normal operation.

Recovery tests will terminate writer processes at controlled points, reopen the
database, and compare observed state with the documented acknowledgement
contract.

## Compression Plan

Compression is postponed until uncompressed blocks are correct and benchmarked.
Candidate techniques include:

- timestamp delta and delta-of-delta encoding;
- variable-length integer encoding;
- XOR encoding for double-precision values;
- optional LZ4 or Zstandard block compression.

Every codec must support bounds-checked decoding, corruption detection, and
versioned selection. Compression ratio will be reported alongside encode and
decode cost.

## Integration Layers

The core engine will be delivered as a C++ library. A CLI will exercise the
same public API used by external programs. Python bindings and an optional HTTP
service may be layered above it without placing networking or Python concerns
inside the storage engine.

ChronoView is planned as a Dear ImGui and ImPlot application for querying data,
viewing engine metrics, and inspecting the WAL, memory tables, segment layout,
indexes, and compaction activity. It is a diagnostic client, not part of the
durability boundary.

## Verification Strategy

Verification will include:

- unit tests for value types, codecs, and indexes;
- model-based tests against a simple in-memory reference implementation;
- randomized operation sequences;
- crash and partial-write recovery tests;
- corruption and malformed-file tests;
- sanitizer builds;
- concurrent read/write stress tests;
- reproducible throughput, latency, memory, and storage benchmarks.

## Implementation Sequence

The dependency order is intentional:

1. build system and value types;
2. reference model and in-memory queries;
3. binary record encoding;
4. write-ahead log and recovery;
5. segment writer and reader;
6. sparse indexes and merged queries;
7. background flushing and snapshots;
8. manifest and compaction;
9. compression and cache;
10. external interfaces and visual inspection tools.

Later components depend on semantics established by earlier components. For
example, overwrite behavior must be settled before WAL replay and compaction can
be implemented consistently.

## Open Design Decisions

These choices will be resolved with short design records before implementation:

- duplicate timestamp and overwrite semantics;
- exact durability modes and synchronization frequency;
- database directory locking and multi-process behavior;
- snapshot visibility guarantees;
- block sizing and segment-selection policy;
- deletion and retention semantics;
- persistent catalog transaction boundaries;
- public error model and ABI expectations.
