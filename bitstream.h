/**
 * @file bitstream.h
 * @brief Fast and lightweight bitstream reader and writer for C.
 *
 * This header-only library provides highly optimized functions to read and write
 * bit-level data from/to memory buffers. It supports both MSB-first and LSB-first
 * bit packing orders, making it ideal for implementing data compression codecs
 * (e.g., DEFLATE, Huffman, JPEG, PNG).
 *
 * Key features:
 * - Safe read/write operations with strict boundary and underrun/overrun checks.
 * - Unchecked variants (fast-paths) designed for critical hot loops.
 * - Byte alignment utilities for formats mixing bit-aligned and byte-aligned data.
 * - Zero-copy bulk byte transfers for uncompressed blocks.
 *
 * @note The bit accumulator relies on a 64-bit cache, allowing up to 32 bits
 * to be read or written safely in a single operation.
 */

#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Bitstream reader context.
 *
 * Maintains the state required to read bits sequentially from a memory buffer.
 * It uses an internal 64-bit cache to fetch bytes ahead of time, minimizing
 * direct memory access overhead during bit-level extraction.
 */
typedef struct {
    const uint8_t *buffer;      /**< Pointer to the read-only source memory buffer. */
    size_t size;                /**< Total size of the buffer in bytes. */
    size_t byte_pos;            /**< Index of the next byte to be fetched from the buffer. */
    uint64_t bit_cache;         /**< 64-bit accumulator holding prefetched bits. */
    unsigned int bits_in_cache; /**< Number of valid, unread bits remaining in the accumulator. */
} bs_reader_t;

/**
 * @brief Bitstream writer context.
 *
 * Maintains the state required to write bits sequentially into a memory buffer.
 * Bits are temporarily accumulated in a 64-bit cache and automatically flushed
 * to the destination memory byte-by-byte as the cache fills up.
 */
typedef struct {
    uint8_t *buffer;            /**< Pointer to the mutable destination memory buffer. */
    size_t size;                /**< Total allocated size of the buffer in bytes. */
    size_t byte_pos;            /**< Index of the next byte to be written to memory. */
    uint64_t bit_cache;         /**< 64-bit accumulator where bits are temporarily injected. */
    unsigned int bits_in_cache; /**< Number of bits currently enqueued and waiting to be flushed. */
    bool overflow; /**< Flag indicating if a write attempt exceeded the buffer capacity. */
} bs_writer_t;

/* ====================================================================
 * Initialization Functions
 * ==================================================================== */

/**
 * @brief Initializes a bitstream reader context.
 *
 * Prepares the reader structure to extract bits from a provided read-only
 * memory buffer. Internal caches and indices are reset to zero.
 *
 * @param bs Pointer to the reader context to initialize.
 * @param data Pointer to the source memory buffer.
 * @param size Total size of the source buffer in bytes.
 */
static inline void bs_reader_init(bs_reader_t *bs, const uint8_t *data, size_t size);

/**
 * @brief Initializes a bitstream writer context.
 *
 * Prepares the writer structure to pack bits into a mutable memory buffer.
 * Internal caches, indices, and the overflow flag are reset to zero/false.
 *
 * @param bs Pointer to the writer context to initialize.
 * @param data Pointer to the destination memory buffer.
 * @param size Maximum available size of the destination buffer in bytes.
 */
static inline void bs_writer_init(bs_writer_t *bs, uint8_t *data, size_t size);

/* ====================================================================
 * Core MSB Safe Read/Write Functions
 * ==================================================================== */

/**
 * @brief Safely reads a single bit (MSB-first) from the bitstream.
 *
 * Convenience function equivalent to calling `bs_read_bits(bs, 1)`.
 *
 * @param bs Pointer to the reader context.
 * @return The extracted bit (0 or 1), or 0 if an underrun occurs (EOF).
 */
static inline uint8_t bs_read_bit(bs_reader_t *bs);

/**
 * @brief Safely reads up to 32 bits (MSB-first) from the bitstream.
 *
 * Enforces boundary checking. If the requested number of bits exceeds the
 * remaining bits available in the stream, the operation fails safely.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to read (0-32).
 * @return The extracted bits right-aligned. Returns 0 if `n == 0` or if
 *         there are insufficient bits left.
 */
static inline uint32_t bs_read_bits(bs_reader_t *bs, unsigned int n);

/**
 * @brief Safely writes a single bit (MSB-first) to the bitstream.
 *
 * Convenience function equivalent to calling `bs_write_bits(bs, value, 1)`.
 *
 * @param bs Pointer to the writer context.
 * @param value The bit value to write (least significant bit is used).
 */
static inline void bs_write_bit(bs_writer_t *bs, uint8_t value);

/**
 * @brief Safely writes up to 32 bits (MSB-first) to the bitstream.
 *
 * Automatically masks the input `value` to ensure only the lower `n` bits are
 * written, preventing dirty upper bits from corrupting previous data. If the
 * underlying buffer runs out of space, the writer sets its internal `overflow` flag.
 *
 * @param bs Pointer to the writer context.
 * @param value The data containing the bits to write.
 * @param n Number of bits to write (0-32).
 */
static inline void bs_write_bits(bs_writer_t *bs, uint32_t value, unsigned int n);

/* ====================================================================
 * MSB Lookahead and Manipulation Functions
 * ==================================================================== */

/**
 * @brief Peeks at up to 32 bits (MSB-first) without advancing the read pointer.
 *
 * Allows inspecting the oncoming bits in the stream without consuming them.
 * Subsequent read or peek operations will see the exact same data.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to peek (0-32).
 * @return The observed bits right-aligned. Returns 0 if `n == 0` or if
 *         there are insufficient bits left.
 */
static inline uint32_t bs_peek_bits(bs_reader_t *bs, unsigned int n);

/**
 * @brief Discards up to 32 bits (MSB-first) from the bitstream.
 *
 * Advances the read pointer forward by `n` bits without returning the data.
 * If `n` is greater than the total bits remaining, the reader safely clips
 * at EOF.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to discard.
 */
static inline void bs_skip_bits(bs_reader_t *bs, unsigned int n);

/* ====================================================================
 * Core LSB Safe Read/Write Functions
 * ==================================================================== */

/**
 * @brief Safely reads up to 32 bits (LSB-first) from the bitstream.
 *
 * Enforces boundary checking. Extracts bits starting from the least
 * significant bit of the current byte.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to read (0-32).
 * @return The extracted bits. Returns 0 if `n == 0` or if an underrun occurs.
 */
static inline uint32_t bs_read_bits_lsb(bs_reader_t *bs, unsigned int n);

/**
 * @brief Safely writes up to 32 bits (LSB-first) to the bitstream.
 *
 * Masks the input `value` to prevent dirty upper bits from corrupting data.
 * Bits are packed into bytes starting from the least significant bit.
 * Sets the `overflow` flag if the buffer runs out of capacity.
 *
 * @param bs Pointer to the writer context.
 * @param value The data containing the bits to write.
 * @param n Number of bits to write (0-32).
 */
static inline void bs_write_bits_lsb(bs_writer_t *bs, uint32_t value, unsigned int n);

/**
 * @brief Peeks at up to 32 bits (LSB-first) without advancing the read pointer.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to peek (0-32).
 * @return The observed bits. Returns 0 if `n == 0` or upon underrun.
 */
static inline uint32_t bs_peek_bits_lsb(bs_reader_t *bs, unsigned int n);

/**
 * @brief Discards up to 32 bits (LSB-first) from the bitstream safely.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to discard.
 */
static inline void bs_skip_bits_lsb(bs_reader_t *bs, unsigned int n);

/* ====================================================================
 * Status and Utility Functions
 * ==================================================================== */

/**
 * @brief Checks if the reader has reached the end of the bitstream.
 *
 * @param bs Pointer to the reader context.
 * @return `true` if all bytes have been consumed and the bit cache is empty;
 *         `false` otherwise.
 */
static inline bool bs_reader_eof(const bs_reader_t *bs);

/**
 * @brief Calculates the total number of unread bits remaining in the stream.
 *
 * Computes the exact amount of bits left by combining the bits residing in
 * the internal cache and the unread bytes remaining in the buffer.
 *
 * @param bs Pointer to the reader context.
 * @return The total number of bits available to read.
 */
static inline size_t bs_reader_bits_left(const bs_reader_t *bs);

/**
 * @brief Flushes pending MSB-first bits and finalizes the stream.
 *
 * Must be called when writing is finished. If the cache contains a partial byte,
 * it pads the remainder of the byte with zeros and dumps it to memory.
 * If the buffer cannot accommodate the final byte, it marks the overflow flag.
 *
 * @param bs Pointer to the writer context.
 */
static inline void bs_writer_flush(bs_writer_t *bs);

/**
 * @brief Flushes pending LSB-first bits and finalizes the stream.
 *
 * Analogous to `bs_writer_flush`, but correctly handles LSB padding
 * (padding zeros are placed in the most significant bits of the final byte).
 *
 * @param bs Pointer to the writer context.
 */
static inline void bs_writer_flush_lsb(bs_writer_t *bs);

/**
 * @brief Retrieves the total number of bytes written to the destination buffer.
 *
 * @warning The returned size reflects bytes physically dumped to memory.
 * If you haven't called `bs_writer_flush`, bits remaining in the cache
 * are not accounted for in this count.
 *
 * @param bs Pointer to the writer context.
 * @return The number of bytes written.
 */
static inline size_t bs_writer_bytes_written(const bs_writer_t *bs);

/* ====================================================================
 * Internal Helpers (Private API)
 * ==================================================================== */

/**
 * @internal
 * @brief Refills the reader's cache (MSB-first).
 *
 * Pulls bytes from the underlying buffer into the 64-bit cache until it
 * holds at least 56 bits. This ensures the next 32-bit read has enough data.
 *
 * @param bs Pointer to the reader context.
 */
static inline void _bs_reader_refill(bs_reader_t *bs);

/**
 * @internal
 * @brief Refills the reader's cache (LSB-first).
 *
 * Pulls bytes from the buffer, shifting them into the higher positions of
 * the 64-bit cache based on the current cache size.
 *
 * @param bs Pointer to the reader context.
 */
static inline void _bs_reader_refill_lsb(bs_reader_t *bs);

/**
 * @internal
 * @brief Dumps the writer's cache to memory (MSB-first).
 *
 * Extracts complete 8-bit chunks from the most significant side of the
 * valid bits in the cache and writes them sequentially to the memory buffer.
 * Sets the `overflow` flag if the buffer is full.
 *
 * @param bs Pointer to the writer context.
 */
static inline void _bs_writer_dump(bs_writer_t *bs);

/**
 * @internal
 * @brief Dumps the writer's cache to memory (LSB-first).
 *
 * Extracts complete 8-bit chunks from the least significant side of the
 * cache and writes them to the memory buffer.
 * Sets the `overflow` flag if the buffer is full.
 *
 * @param bs Pointer to the writer context.
 */
static inline void _bs_writer_dump_lsb(bs_writer_t *bs);

/**
 * @brief Reads `n` bits (MSB-first) without safety checks.
 *
 * @warning Use exclusively in hot loops to maximize performance.
 * @pre The caller must guarantee that `bs_reader_bits_left(bs) >= n` and `n > 0`.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to read (1-32).
 * @return The extracted bits.
 */
static inline uint32_t bs_read_bits_unchecked(bs_reader_t *bs, unsigned int n);

/**
 * @brief Peeks at `n` bits (MSB-first) without consuming them and without safety checks.
 *
 * @pre `bs_reader_bits_left(bs) >= n` and `n > 0`.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to peek (1-32).
 * @return The observed bits.
 */
static inline uint32_t bs_peek_bits_unchecked(bs_reader_t *bs, unsigned int n);

/**
 * @brief Discards `n` bits (MSB-first) from the cache without safety checks.
 *
 * @pre The `n` bits must already be loaded in the internal cache (usually
 * verified via a prior peek operation).
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to discard.
 */
static inline void bs_skip_bits_unchecked(bs_reader_t *bs, unsigned int n);

/**
 * @brief Writes `n` bits (MSB-first) without masking the value or validating buffer space.
 *
 * @warning This function does not apply a protective mask. If `value` contains
 * set bits beyond the `n`-th position, it will corrupt the bitstream.
 * @pre `n > 0`, `n <= 32`, and `value` must be clean (no garbage bits above `n`).
 *
 * @param bs Pointer to the writer context.
 * @param value Value to write (must be clean of garbage in upper bits).
 * @param n Number of bits to write (1-32).
 */
static inline void bs_write_bits_unchecked(bs_writer_t *bs, uint32_t value, unsigned int n);

/**
 * @brief Reads `n` bits (LSB-first) without safety checks.
 *
 * @pre The caller must guarantee that `bs_reader_bits_left(bs) >= n` and `n > 0`.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to read (1-32).
 * @return The extracted bits.
 */
static inline uint32_t bs_read_bits_lsb_unchecked(bs_reader_t *bs, unsigned int n);

/**
 * @brief Peeks at `n` bits (LSB-first) without consuming them and without safety checks.
 *
 * @pre `bs_reader_bits_left(bs) >= n` and `n > 0`.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to peek (1-32).
 * @return The observed bits.
 */
static inline uint32_t bs_peek_bits_lsb_unchecked(bs_reader_t *bs, unsigned int n);

/**
 * @brief Discards `n` bits (LSB-first) from the cache without safety checks.
 *
 * @pre The `n` bits must already be loaded in the internal cache.
 *
 * @param bs Pointer to the reader context.
 * @param n Number of bits to discard.
 */
static inline void bs_skip_bits_lsb_unchecked(bs_reader_t *bs, unsigned int n);

/**
 * @brief Writes `n` bits (LSB-first) without masking the value or validating buffer space.
 *
 * @warning Requires `value` to be clean above the `n`-th bit to prevent corruption.
 * @pre `n > 0` and `n <= 32`.
 *
 * @param bs Pointer to the writer context.
 * @param value Value to write.
 * @param n Number of bits to write (1-32).
 */
static inline void bs_write_bits_lsb_unchecked(bs_writer_t *bs, uint32_t value, unsigned int n);

/**
 * @brief Aligns the reader to the next byte boundary (MSB-first).
 *
 * Discards the remaining bits of the partially read byte. If the reader
 * is already byte-aligned, this function is a no-op.
 *
 * @param bs Pointer to the reader context.
 */
static inline void bs_reader_align(bs_reader_t *bs);

/**
 * @brief Aligns the writer to the next byte boundary (MSB-first).
 *
 * If there are pending bits in a partial byte, it pads the rest of the
 * byte with zeros until complete, forcing a flush to the buffer.
 *
 * @param bs Pointer to the writer context.
 */
static inline void bs_writer_align(bs_writer_t *bs);

/**
 * @brief Aligns the reader to the next byte boundary (LSB-first).
 *
 * Discards the remaining bits by shifting the internal cache.
 *
 * @param bs Pointer to the reader context.
 */
static inline void bs_reader_align_lsb(bs_reader_t *bs);

/**
 * @brief Aligns the writer to the next byte boundary (LSB-first).
 *
 * Pads with zeros until the current byte is complete.
 *
 * @param bs Pointer to the writer context.
 */
static inline void bs_writer_align_lsb(bs_writer_t *bs);

/**
 * @brief Copies blocks of bytes directly from the stream to a destination buffer.
 *
 * Uses a direct `memcpy` (bypassing the bit accumulator) to maximize speed.
 * Ideal for copying sections of literal (uncompressed) data.
 *
 * @pre The reader MUST be byte-aligned (call `bs_reader_align` beforehand).
 *
 * @param bs Pointer to the reader context.
 * @param dst Destination buffer where the bytes will be copied.
 * @param n_bytes Number of bytes to copy.
 * @return `true` if all bytes were successfully copied; `false` if the reader
 *         was not byte-aligned or if there were not enough bytes left in the buffer.
 */
static inline bool bs_read_bytes(bs_reader_t *bs, uint8_t *dst, size_t n_bytes);

/**
 * @brief Writes blocks of bytes directly from a source buffer into the stream.
 *
 * @pre The writer MUST be byte-aligned (call `bs_writer_align` beforehand).
 *
 * @param bs Pointer to the writer context.
 * @param src Source buffer containing the data to write.
 * @param n_bytes Number of bytes to write.
 * @return `true` if successfully written; `false` if the writer was not
 *         aligned or if a buffer overflow occurred.
 */
static inline bool bs_write_bytes(bs_writer_t *bs, const uint8_t *src, size_t n_bytes);

static inline void bs_reader_init(bs_reader_t *bs, const uint8_t *data, size_t size) {
    bs->buffer = data;
    bs->size = size;
    bs->byte_pos = 0;
    bs->bit_cache = 0;
    bs->bits_in_cache = 0;
}

static inline void bs_writer_init(bs_writer_t *bs, uint8_t *data, size_t size) {
    bs->buffer = data;
    bs->size = size;
    bs->byte_pos = 0;
    bs->bit_cache = 0;
    bs->bits_in_cache = 0;
    bs->overflow = false;
}

static inline uint8_t bs_read_bit(bs_reader_t *bs) {
    return (uint8_t)bs_read_bits(bs, 1);
}

static inline uint32_t bs_read_bits(bs_reader_t *bs, unsigned int n) {
    if (n == 0) return 0;
    _bs_reader_refill(bs);

    if (bs->bits_in_cache < n) return 0;

    bs->bits_in_cache -= n;
    uint32_t result = (uint32_t)((bs->bit_cache >> bs->bits_in_cache) & ((1ULL << n) - 1));

    return result;
}

static inline void bs_write_bit(bs_writer_t *bs, uint8_t value) {
    bs->bit_cache = bs->bit_cache << 1 | (value & 1);
    bs->bits_in_cache++;

    if (bs->bits_in_cache >= 8) {
        _bs_writer_dump(bs);
    }
}

static inline void bs_write_bits(bs_writer_t *bs, uint32_t value, unsigned int n) {
    assert(n <= 32);
    if (n == 0) return;

    uint64_t mask = (1ULL << n) - 1;
    uint64_t clean_value = value & mask;

    bs->bit_cache = (bs->bit_cache << n) | clean_value;
    bs->bits_in_cache += n;

    if (bs->bits_in_cache >= 8) {
        _bs_writer_dump(bs);
    }
}

static inline uint32_t bs_peek_bits(bs_reader_t *bs, unsigned int n) {
    if (n == 0) return 0;
    _bs_reader_refill(bs);

    if (bs->bits_in_cache < n) return 0;

    uint32_t result = (uint32_t)((bs->bit_cache >> (bs->bits_in_cache - n)) & ((1ULL << n) - 1));

    return result;
}

static inline void bs_skip_bits(bs_reader_t *bs, unsigned int n) {
    if (n == 0) return;
    _bs_reader_refill(bs);

    if (bs->bits_in_cache < n) {
        bs->bits_in_cache = 0;
        return;
    }

    bs->bits_in_cache -= n;
}

static inline uint32_t bs_read_bits_lsb(bs_reader_t *bs, unsigned int n) {
    if (n == 0) return 0;
    _bs_reader_refill_lsb(bs);

    if (bs->bits_in_cache < n) return 0;

    uint32_t result = (uint32_t)(bs->bit_cache & ((1ULL << n) - 1));
    bs->bit_cache >>= n;
    bs->bits_in_cache -= n;

    return result;
}

static inline void bs_write_bits_lsb(bs_writer_t *bs, uint32_t value, unsigned int n) {
    assert(n <= 32);
    if (n == 0) return;

    uint64_t mask = (1ULL << n) - 1;
    uint64_t clean_value = value & mask;

    bs->bit_cache |= (clean_value << bs->bits_in_cache);

    bs->bits_in_cache += n;

    if (bs->bits_in_cache >= 8) {
        _bs_writer_dump_lsb(bs);
    }
}

static inline uint32_t bs_peek_bits_lsb(bs_reader_t *bs, unsigned int n) {
    if (n == 0) return 0;
    _bs_reader_refill_lsb(bs);

    if (bs->bits_in_cache < n) return 0;

    uint32_t result = (uint32_t)(bs->bit_cache & ((1ULL << n) - 1));
    return result;
}

static inline void bs_skip_bits_lsb(bs_reader_t *bs, unsigned int n) {
    if (n == 0) return;
    _bs_reader_refill_lsb(bs);

    if (bs->bits_in_cache < n) {
        bs->bits_in_cache = 0;
        bs->bit_cache = 0;
        return;
    }

    bs->bit_cache >>= n;
    bs->bits_in_cache -= n;
}

static inline void bs_writer_flush_lsb(bs_writer_t *bs) {
    if (bs->bits_in_cache == 0) return;

    if (bs->overflow || bs->bits_in_cache >= 8) {
        _bs_writer_dump_lsb(bs);
        if (bs->bits_in_cache == 0) return;
        if (bs->bits_in_cache >= 8) {
            bs->overflow = true;
            return;
        }
    }

    unsigned int padding_bits = 8 - bs->bits_in_cache;
    bs_write_bits_lsb(bs, 0, padding_bits);
}

static inline bool bs_reader_eof(const bs_reader_t *bs) {
    return (bs->byte_pos >= bs->size) && (bs->bits_in_cache == 0);
}

static inline size_t bs_reader_bits_left(const bs_reader_t *bs) {
    if (bs->byte_pos >= bs->size) {
        return (size_t)bs->bits_in_cache;
    }

    size_t bytes_left = bs->size - bs->byte_pos;
    return (bytes_left * 8) + bs->bits_in_cache;
}

static inline void bs_writer_flush(bs_writer_t *bs) {
    if (bs->bits_in_cache == 0) return;

    if (bs->overflow || bs->bits_in_cache >= 8) {
        /* El buffer desbordo y hay bits huerfanos en cache.
         * Intentar un ultimo dump por si hay espacio residual. */
        _bs_writer_dump(bs);
        if (bs->bits_in_cache == 0) return;
        if (bs->bits_in_cache >= 8) {
            /* Aun no caben: no podemos alinear, solo marcar overflow */
            bs->overflow = true;
            return;
        }
    }

    unsigned int padding_bits = 8 - bs->bits_in_cache;
    bs_write_bits(bs, 0, padding_bits);
}

static inline size_t bs_writer_bytes_written(const bs_writer_t *bs) {
    return bs->byte_pos;
}

static inline void _bs_reader_refill(bs_reader_t *bs) {
    while (bs->bits_in_cache <= 56) {
        if (bs->byte_pos >= bs->size) {
            break;
        }

        uint8_t next_byte = bs->buffer[bs->byte_pos];

        bs->bit_cache = (bs->bit_cache << 8) | next_byte;

        bs->bits_in_cache += 8;
        bs->byte_pos++;
    }
}

static inline void _bs_reader_refill_lsb(bs_reader_t *bs) {
    while (bs->bits_in_cache <= 56) {
        if (bs->byte_pos >= bs->size) {
            break;
        }

        uint8_t next_byte = bs->buffer[bs->byte_pos];

        bs->bit_cache |= (uint64_t)next_byte << bs->bits_in_cache;

        bs->bits_in_cache += 8;
        bs->byte_pos++;
    }
}

static inline void _bs_writer_dump(bs_writer_t *bs) {
    while (bs->bits_in_cache >= 8) {
        if (bs->byte_pos >= bs->size) {
            bs->overflow = true;
            break;
        }
        unsigned int shift_amount = bs->bits_in_cache - 8;
        uint8_t byte_to_write = (uint8_t)((bs->bit_cache >> shift_amount) & 0xFF);

        bs->buffer[bs->byte_pos] = byte_to_write;
        bs->byte_pos++;

        bs->bits_in_cache -= 8;
    }

    bs->bit_cache &= ((1ULL << bs->bits_in_cache) - 1);
}

static inline void _bs_writer_dump_lsb(bs_writer_t *bs) {
    while (bs->bits_in_cache >= 8) {
        if (bs->byte_pos >= bs->size) {
            bs->overflow = true;
            break;
        }
        uint8_t byte_to_write = (uint8_t)(bs->bit_cache & 0xFF);

        bs->buffer[bs->byte_pos] = byte_to_write;
        bs->byte_pos++;

        bs->bit_cache >>= 8;
        bs->bits_in_cache -= 8;
    }
}

static inline uint32_t bs_read_bits_unchecked(bs_reader_t *bs, unsigned int n) {
    _bs_reader_refill(bs);
    bs->bits_in_cache -= n;
    return (uint32_t)((bs->bit_cache >> bs->bits_in_cache) & ((1ULL << n) - 1));
}

static inline uint32_t bs_peek_bits_unchecked(bs_reader_t *bs, unsigned int n) {
    _bs_reader_refill(bs);
    return (uint32_t)((bs->bit_cache >> (bs->bits_in_cache - n)) & ((1ULL << n) - 1));
}

static inline void bs_skip_bits_unchecked(bs_reader_t *bs, unsigned int n) {
    bs->bits_in_cache -= n;
}

static inline void bs_write_bits_unchecked(bs_writer_t *bs, uint32_t value, unsigned int n) {
    bs->bit_cache = (bs->bit_cache << n) | value;
    bs->bits_in_cache += n;
    if (bs->bits_in_cache >= 8) {
        _bs_writer_dump(bs);
    }
}

static inline uint32_t bs_read_bits_lsb_unchecked(bs_reader_t *bs, unsigned int n) {
    _bs_reader_refill_lsb(bs);
    uint32_t result = (uint32_t)(bs->bit_cache & ((1ULL << n) - 1));
    bs->bit_cache >>= n;
    bs->bits_in_cache -= n;
    return result;
}

static inline uint32_t bs_peek_bits_lsb_unchecked(bs_reader_t *bs, unsigned int n) {
    _bs_reader_refill_lsb(bs);
    return (uint32_t)(bs->bit_cache & ((1ULL << n) - 1));
}

static inline void bs_skip_bits_lsb_unchecked(bs_reader_t *bs, unsigned int n) {
    bs->bit_cache >>= n;
    bs->bits_in_cache -= n;
}

static inline void bs_write_bits_lsb_unchecked(bs_writer_t *bs, uint32_t value, unsigned int n) {
    bs->bit_cache |= ((uint64_t)value << bs->bits_in_cache);
    bs->bits_in_cache += n;
    if (bs->bits_in_cache >= 8) {
        _bs_writer_dump_lsb(bs);
    }
}

static inline void bs_reader_align(bs_reader_t *bs) {
    unsigned int res = bs->bits_in_cache % 8;
    if (res > 0) {
        bs->bits_in_cache -= res;
    }
}

static inline void bs_writer_align(bs_writer_t *bs) {
    unsigned int res = bs->bits_in_cache % 8;
    if (res > 0) {
        bs_write_bits(bs, 0, 8 - res);
    }
}

static inline void bs_reader_align_lsb(bs_reader_t *bs) {
    unsigned int res = bs->bits_in_cache % 8;
    if (res > 0) {
        bs->bit_cache >>= res;
        bs->bits_in_cache -= res;
    }
}

static inline void bs_writer_align_lsb(bs_writer_t *bs) {
    unsigned int res = bs->bits_in_cache % 8;
    if (res > 0) {
        bs_write_bits_lsb(bs, 0, 8 - res);
    }
}

static inline bool bs_read_bytes(bs_reader_t *bs, uint8_t *dst, size_t n_bytes) {
    if (bs->bits_in_cache % 8 != 0) return false; /* not aligned */

    bs->byte_pos -= (bs->bits_in_cache / 8);
    bs->bits_in_cache = 0;
    bs->bit_cache = 0;

    size_t available_bytes = bs->size - bs->byte_pos;
    if (n_bytes > available_bytes) return false;

    memcpy(dst, bs->buffer + bs->byte_pos, n_bytes);
    bs->byte_pos += n_bytes;
    return true;
}

static inline bool bs_write_bytes(bs_writer_t *bs, const uint8_t *src, size_t n_bytes) {
    if (bs->bits_in_cache != 0) return false; /* not aligned */
    if (bs->overflow) return false;

    size_t available = bs->size - bs->byte_pos;
    if (n_bytes > available) {
        bs->overflow = true;
        return false;
    }

    memcpy(bs->buffer + bs->byte_pos, src, n_bytes);
    bs->byte_pos += n_bytes;
    return true;
}

#endif // BITSTREAM_H
