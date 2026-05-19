# bitstream.h

`bitstream.h` is a high-performance, **header-only** C library designed for efficient manipulation of arbitrary bit streams.

---

## Why `bitstream.h`?

C's standard I/O works in 8-bit byte-aligned blocks. However, most modern data formats — such as audio/video codecs, network protocols, and compression algorithms like DEFLATE or Huffman coding — store information in non-aligned widths (e.g. 3, 5, or 12 bits).

This library provides the low-level primitives needed to extract and pack such data, avoiding alignment overhead and minimizing memory access through a **64-bit bit-cache (accumulator) architecture**.

---

# Development Roadmap

The project is structured in four iterative phases, ensuring each component is robust and verifiable before moving on to more complex optimizations.

---

## Phase 1: The Reader (MSB-First)

### Goal

Build the core read engine. Implement bit extraction from a memory buffer, handling automatic cache refill.

### Key functions

```c
bs_reader_init(bs_reader_t *bs, const uint8_t *data, size_t size);
```

Initializes the reader state (pointer, size, and cache).

```c
_bs_reader_refill(bs_reader_t *bs);
```

Internal function that loads bytes from RAM into the cache when it drops below 56 bits.

```c
bs_read_bits(bs_reader_t *bs, int n);
```

Extracts `n` bits from the stream.

```c
bs_read_bit(bs_reader_t *bs);
```

Optimized alias for reading 1 bit.

```c
bs_reader_bits_left(const bs_reader_t *bs);
```

Returns the number of bits remaining in the stream.

```c
bs_reader_eof(const bs_reader_t *bs);
```

Checks whether the end of the stream has been reached.

---

## Phase 2: The Writer (MSB-First)

### Goal

Implement the counterpart to the reader: efficient writing into buffers, handling cache overflow back to RAM.

### Key functions

```c
bs_writer_init(bs_writer_t *bs, uint8_t *data, size_t size);
```

Initializes the destination buffer.

```c
_bs_writer_dump(bs_writer_t *bs);
```

Flushes the cache to RAM when 8 or more bits have accumulated.

```c
bs_write_bits(bs_writer_t *bs, uint32_t value, int n);
```

Packs `n` bits into the accumulator.

```c
bs_write_bit(bs_writer_t *bs, uint8_t value);
```

Optimized alias for writing 1 bit.

```c
bs_writer_flush(bs_writer_t *bs);
```

Byte-aligns the output by padding with zeros and performs the final flush.

```c
bs_writer_bytes_written(const bs_writer_t *bs);
```

Returns the actual number of bytes written.

---

## Phase 3: Versatility (Lookahead & LSB-First)

### Goal

Prepare the library for real-world codecs and protocols that require inspecting the stream without consuming bits, or that use LSB-first bit ordering.

### Key functions

```c
bs_peek_bits(bs_reader_t *bs, int n);
```

Reads bits without advancing the stream pointer. Needed for Huffman tree parsing and lookahead.

```c
bs_read_bits_lsb(bs_reader_t *bs, int n);
bs_write_bits_lsb(bs_writer_t *bs, ...);
```

Variants for LSB-first protocols.

```c
bs_writer_set_order(bs_writer_t *bs, bool lsb);
```

MSB/LSB mode selector.

---

## Phase 4: Robustness & Fast-Paths

### Goal

Extreme optimization for production environments, removing redundant validation checks in hot loops.

### Key functions

```c
bs_read_bits_fast(bs_reader_t *bs, int n);
```

Version without `_refill()` validation. Assumes the buffer holds sufficient data.

```c
bs_write_bits_fast(bs_writer_t *bs, uint32_t value, int n);
```

Version without overflow validation. Assumes sufficient buffer capacity.

```c
bs_align(bs_writer_t *bs);
```

Forces alignment to the next byte boundary via padding.

---

# Design Philosophy

## Header-only

Drop-in integration with any project.

Simply copy `bitstream.h` and use it — no external dependencies, no separate compilation step.

---

## Zero-cost Abstractions

All functions are declared as:

```c
static inline
```

This allows the compiler to inline the code directly at the call site, eliminating function call overhead entirely.

---

## Lazy Evaluation

RAM is only accessed when:

* The 64-bit accumulator is empty (reader).
* The accumulator is full (writer).

This minimizes memory accesses and improves performance under intensive workloads.

---

## Standard C

Full compatibility with:

* C99
* `stdint.h`
* `stddef.h`

---

# Requirements

* A C99-compatible compiler or newer.
* Architecture with 64-bit type support (`uint64_t` required for the cache).
