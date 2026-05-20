# bitstream.h

High-performance, header-only C99 library for reading and writing arbitrary-width bit streams.

## Why?

C's standard I/O works in byte-aligned blocks. Most real-world binary formats — audio/video codecs, network protocols, compression algorithms like DEFLATE, Huffman coding — store data in non-aligned widths (3, 5, 12 bits, etc.).

`bitstream.h` provides the low-level primitives to extract and pack these fields efficiently, using a 64-bit accumulator that minimizes memory access and avoids alignment overhead.

## Quick Start

Copy `bitstream.h` into your project. No build step, no dependencies.

```c
#include "bitstream.h"

/* Read 4 bits from a buffer */
uint8_t data[] = { 0xA5 };          /* 1010 0101 */
bs_reader_t r;
bs_reader_init(&r, data, sizeof(data));

uint32_t hi = bs_read_bits(&r, 4);  /* 10 (0b1010) */
uint32_t lo = bs_read_bits(&r, 4);  /*  5 (0b0101) */
```

```c
/* Write variable-width fields into a buffer */
uint8_t buf[4] = {0};
bs_writer_t w;
bs_writer_init(&w, buf, sizeof(buf));

bs_write_bits(&w, 0x1F, 5);    /* 5 bits  */
bs_write_bits(&w, 0xAB, 8);    /* 8 bits  */
bs_write_bits(&w, 1, 1);       /* 1 bit   */
bs_writer_flush(&w);            /* pad to byte boundary */

size_t bytes = bs_writer_bytes_written(&w);
```

## API Reference

### Reader

| Function | Description |
|---|---|
| `bs_reader_init(bs, data, size)` | Initialize reader over a memory buffer |
| `bs_read_bits(bs, n)` | Extract `n` bits (1–32) from the stream |
| `bs_read_bit(bs)` | Extract a single bit |
| `bs_reader_bits_left(bs)` | Bits remaining in the stream |
| `bs_reader_eof(bs)` | `true` when the stream is exhausted |

### Writer

| Function | Description |
|---|---|
| `bs_writer_init(bs, data, size)` | Initialize writer over a memory buffer |
| `bs_write_bits(bs, value, n)` | Pack `n` bits (1–32) into the stream |
| `bs_write_bit(bs, value)` | Pack a single bit |
| `bs_writer_flush(bs)` | Pad to byte boundary and flush |
| `bs_writer_bytes_written(bs)` | Bytes written so far |

The writer sets `bs->overflow = true` if the output buffer fills up. Already-written bytes are never corrupted.

## Examples

The `examples/` directory contains complete, compilable programs:

| Example | Description |
|---|---|
| `decode_mp3_header.c` | Parse a 32-bit MP3 frame header into its 13 fields |
| `sensor_protocol.c` | Encode/decode a compact IoT sensor packet (full round-trip) |

```
make examples
./examples/decode_mp3_header
./examples/sensor_protocol
```

## Building & Testing

```
make test          # debug build (asserts enabled), run tests
make test-all      # run tests in debug, release (-O2), and sanitizer modes
make examples      # compile the examples
make clean         # remove all binaries
```

`make test-all` compiles and runs the full suite three times: debug (`-O0`, asserts active), release (`-O2`, `-DNDEBUG`), and with AddressSanitizer + UBSanitizer. This catches undefined behavior that only manifests under optimization.

## Project Structure

```
├── bitstream.h                   # The library (single header)
├── Makefile
├── tests/
│   ├── test.h                    # Minimal test harness
│   ├── run_all.c                 # Test runner
│   ├── test_reader.c             # Phase 1: reader tests
│   ├── test_writer.c             # Phase 2: writer tests
│   ├── test_regression.c         # Bug regression tests
│   └── test_integration.c        # End-to-end scenarios
└── examples/
    ├── decode_mp3_header.c
    └── sensor_protocol.c
```

## Design

**Header-only** — copy one file, done. No build system integration needed.

**Zero-cost abstractions** — every function is `static inline`, so the compiler inlines them at the call site with no function-call overhead.

**Lazy memory access** — RAM is only touched when the 64-bit accumulator is empty (reader) or full (writer), minimizing cache pressure in tight loops.

**Standard C99** — depends only on `stdint.h`, `stddef.h`, and `stdbool.h`. Works with GCC, Clang, MSVC, and any conforming compiler. Requires `uint64_t` support.

## Roadmap

- [x] **Phase 1** — Reader (MSB-first): bit extraction, automatic cache refill, EOF tracking
- [x] **Phase 2** — Writer (MSB-first): bit packing, flush/padding, overflow detection
- [x] **Phase 3** — Lookahead (`bs_peek_bits`) and LSB-first read/write variants
- [ ] **Phase 4** — Fast-paths: unchecked read/write for hot loops, byte alignment

## License

MIT
