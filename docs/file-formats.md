# ChronoStore File Formats

This document records the implemented version-one persistent layouts. It is a
review aid and corruption-testing reference, not a compatibility promise for a
pre-1.0 release.

## Common Encoding Rules

- All integers use explicit little-endian encoding.
- Widths are fixed with `std::uint8_t`, `std::uint16_t`, `std::uint32_t`, and
  `std::uint64_t`.
- Signed timestamps are preserved by bit-casting `int64_t` to `uint64_t`.
- Values are finite IEEE 754 binary64 numbers bit-cast to `uint64_t`.
- Strings are `uint32 length` followed by exactly that many bytes.
- A series key is `measurement`, `tag_count`, then repeated tag key/value
  strings in strictly increasing key order.
- Checksums use CRC32C (Castagnoli), stored as a little-endian `uint32_t`.
- CRC32C detects accidental corruption; it is not a cryptographic MAC.

The code does not serialize native structs, padding, pointers, container
layouts, or implementation-defined hash values.

## Database Directory

```text
LOCK
chronostore.wal
MANIFEST
segment-00000000000000000001.cst
segment-00000000000000000002.cst
```

`LOCK` is an operating-system lock file, not a data format. Segment filenames
use a zero-padded generation. Temporary atomic-write files append `.tmp` and
are not database state.

## WAL Record Version 1

The active WAL is a concatenation of independently framed records.

### Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `CSWL` |
| 4 | 1 | Version, currently `1` |
| 5 | 1 | Record type, currently `PUT = 1` |
| 6 | 4 | Payload byte length |
| 10 | 4 | Bitwise inverse of payload length |

The fixed header is 14 bytes.

### PUT Payload

```text
measurement string
uint32 tag_count
repeated tag_count times:
    tag key string
    tag value string
uint64 timestamp_bits
uint64 value_bits
```

A 4-byte CRC32C follows the payload and covers the complete header plus
payload. The maximum payload is 16 MiB.

The length/inverse pair lets recovery reject implausible framing before using
the length. After a complete frame size is known, the checksum is verified
before version, record type, or payload interpretation.

### Recovery Rules

- Every complete valid record is replayed in file order.
- A final prefix that cannot form a complete record is considered an
  interrupted append and is durably truncated.
- Corruption in a complete record is rejected; recovery reports its starting
  byte offset.
- Bytes after a corrupt complete record are not searched for a new magic value.
  Recovery never guesses a resynchronization point.

## Segment Block Version 1

A segment block contains one series and 1 to 256 strictly increasing samples.

### Block Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `CSBK` |
| 4 | 2 | Version, currently `1` |
| 6 | 2 | Flags, currently `0` |
| 8 | 4 | Payload byte length |
| 12 | 4 | Bitwise inverse of payload length |
| 16 | 4 | Sample count |

The fixed header is 20 bytes.

### Block Payload

```text
series key
repeated sample_count times:
    uint64 timestamp_bits
    uint64 value_bits
```

A 4-byte CRC32C follows the payload and covers the header plus payload. The
maximum encoded payload is 16 MiB. Version-one flags are zero because blocks
are deliberately uncompressed.

The decoder validates checksum, version, flags, sample count, canonical tags,
finite values, strict timestamp order, exact payload consumption, and maximum
sizes.

## Segment File Version 1

```text
32-byte file header
segment block 0
segment block 1
...
checksummed sparse index
32-byte file footer
```

### File Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `CSGF` |
| 4 | 2 | Version, currently `1` |
| 6 | 2 | Flags, currently `0` |
| 8 | 4 | Block count |
| 12 | 8 | Index file offset |
| 20 | 8 | Index byte size |
| 28 | 4 | CRC32C of bytes 0 through 27 |

### Sparse Index Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `CSIX` |
| 4 | 2 | Version, currently `1` |
| 6 | 2 | Flags, currently `0` |
| 8 | 4 | Entry count |
| 12 | 4 | Payload byte length |
| 16 | 4 | Bitwise inverse of payload length |

Each index entry is:

```text
uint64 block_file_offset
uint32 encoded_block_size
uint32 sample_count
uint64 first_timestamp_bits
uint64 last_timestamp_bits
series key
```

A 4-byte CRC32C follows the index payload and covers the index header plus
payload. The index payload is limited to 64 MiB and one million entries.

Entries must describe a contiguous block region starting immediately after the
file header. They are strictly ordered by series and non-overlapping timestamp
range. The calculated end of the final block must equal the index offset.

### File Footer

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `CSFT` |
| 4 | 2 | Version, currently `1` |
| 6 | 2 | Flags, currently `0` |
| 8 | 8 | Index file offset |
| 16 | 8 | Index byte size |
| 24 | 4 | Block count |
| 28 | 4 | CRC32C of bytes 0 through 27 |

Header and footer values must agree exactly. The index must end immediately
before the footer, so trailing or overlapping regions are rejected.

### Lazy Block Validation

Opening a segment validates the file size, header, footer, index bounds, index
checksum, entry ordering, and all metadata relationships. Data blocks are read
and checksummed only when a query touches them. After decoding, a block's
series, timestamp bounds, sample count, and encoded size must match its index
entry.

This separates inexpensive open-time metadata validation from on-demand data
I/O while still detecting a corrupt block before returning any of its samples.

## Manifest Version 1

The manifest is one checksummed frame.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `CSMF` |
| 4 | 2 | Version, currently `1` |
| 6 | 2 | Flags, currently `0` |
| 8 | 8 | Generation |
| 16 | 8 | Logical sample count in live segments |
| 24 | 4 | Live segment count |
| 28 | 4 | Payload byte length |
| 32 | 4 | Bitwise inverse of payload length |

The 36-byte header is followed by repeated segment filename strings and a
4-byte CRC32C covering header plus payload. The payload is limited to 16 MiB
and 100,000 segment names.

Filenames must be leaf paths ending in `.cst`; path separators, parent paths,
and duplicates are rejected. The manifest generation determines the next
segment filename. The manifest's segment list, not a directory scan, defines
the live database state.

## Atomic File Publication

Segments and manifests use the same publication pattern:

1. Encode the complete target file in memory.
2. Write `<target>.tmp` with truncation.
3. Flush the C++ stream and close it.
4. Synchronize the temporary file.
5. Atomically replace the target name.
6. Synchronize the parent directory on POSIX systems.

Windows uses `MoveFileExW` with replace-existing and write-through flags. macOS
tries `F_FULLFSYNC` and falls back to `fsync`; other POSIX systems use `fsync`.

The fixed temporary name is safe because the database directory has one
exclusive process owner and mutations are serialized within that process.

## Compatibility Policy

Unknown versions and nonzero unknown flags are rejected. The v0.1 code has no
format upgrader and does not silently reinterpret old layouts. A future stable
release should define explicit reader compatibility and migration before
changing any version-one field.
