/*
 * fast_decode.c — Huffman decoding: safe vs unchecked (fast-path).
 *
 * Demonstrates the unchecked variants (bs_peek_bits_unchecked,
 * bs_skip_bits_unchecked) in a hot decode loop, showing the
 * pattern used by real codecs for maximum throughput.
 *
 * The program:
 *   1. Encodes a message with a simple Huffman table (MSB-first).
 *   2. Decodes it twice: once with safe functions, once with unchecked.
 *   3. Verifies both produce the same output.
 *   4. Prints a step-by-step trace of the unchecked decode.
 *
 * Code table (prefix-free, MSB-first):
 *
 *   Code     Bits  Symbol
 *   ──────  ─────  ──────
 *   0         1    'e'      (most frequent → shortest code)
 *   10        2    't'
 *   110       3    'a'
 *   1110      4    ' '      (space)
 *   11110     5    'h'
 *   111110    6    'r'
 *   111111    6    '\0'     (end-of-message sentinel)
 *
 * Compile:
 *   gcc -std=c99 -Wall -Wextra -I. -o fast_decode fast_decode.c
 *
 * Run:
 *   ./fast_decode
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "bitstream.h"

/* ── Huffman table ──────────────────────────────────────────────── */

#define MAX_CODE_BITS 6
#define SENTINEL '\0'

typedef struct {
    char     symbol;
    uint32_t code;
    unsigned int bits;
} huff_entry_t;

static const huff_entry_t HUFF_TABLE[] = {
    { 'e',      0x00, 1 },  /* 0       */
    { 't',      0x02, 2 },  /* 10      */
    { 'a',      0x06, 3 },  /* 110     */
    { ' ',      0x0E, 4 },  /* 1110    */
    { 'h',      0x1E, 5 },  /* 11110   */
    { 'r',      0x3E, 6 },  /* 111110  */
    { SENTINEL, 0x3F, 6 },  /* 111111  */
};
static const int HUFF_SIZE = (int)(sizeof(HUFF_TABLE) / sizeof(HUFF_TABLE[0]));

/* Find the table entry for a given symbol */
static const huff_entry_t *huff_find(char symbol) {
    for (int i = 0; i < HUFF_SIZE; i++) {
        if (HUFF_TABLE[i].symbol == symbol) return &HUFF_TABLE[i];
    }
    return NULL;
}

/* Match a peeked bit pattern against the table.
 * In a real codec this would be a lookup table for O(1) decode. */
static const huff_entry_t *huff_match(uint32_t window) {
    for (int i = 0; i < HUFF_SIZE; i++) {
        /* Extract the top `bits` from the window and compare */
        unsigned int bits = HUFF_TABLE[i].bits;
        uint32_t candidate = window >> (MAX_CODE_BITS - bits);
        if (candidate == HUFF_TABLE[i].code) return &HUFF_TABLE[i];
    }
    return NULL;
}

/* ── Encoder ────────────────────────────────────────────────────── */

static size_t huffman_encode(const char *msg, uint8_t *out, size_t out_size) {
    bs_writer_t bw;
    bs_writer_init(&bw, out, out_size);

    for (const char *p = msg; *p != '\0'; p++) {
        const huff_entry_t *e = huff_find(*p);
        assert(e != NULL);
        bs_write_bits(&bw, e->code, e->bits);
    }

    /* Write the sentinel to mark end-of-message */
    const huff_entry_t *sentinel = huff_find(SENTINEL);
    assert(sentinel != NULL);
    bs_write_bits(&bw, sentinel->code, sentinel->bits);

    bs_writer_flush(&bw);
    assert(!bw.overflow);

    return bs_writer_bytes_written(&bw);
}

/* ── Safe decoder ───────────────────────────────────────────────── */

static void huffman_decode_safe(
    const uint8_t *data, size_t len,
    char *out, size_t out_size)
{
    bs_reader_t br;
    bs_reader_init(&br, data, len);

    size_t pos = 0;
    while (pos < out_size - 1) {
        /* Peek the maximum code length */
        uint32_t window = bs_peek_bits(&br, MAX_CODE_BITS);
        const huff_entry_t *e = huff_match(window);
        assert(e != NULL);

        if (e->symbol == SENTINEL) {
            bs_skip_bits(&br, e->bits);
            break;
        }

        out[pos++] = e->symbol;
        bs_skip_bits(&br, e->bits);
    }
    out[pos] = '\0';
}

/* ── Unchecked (fast-path) decoder ──────────────────────────────── */
/*
 * In a real codec, this is the hot loop that runs millions of times.
 * The unchecked variants skip all boundary checks:
 *   - No `if (n == 0)` guard (we always peek MAX_CODE_BITS > 0)
 *   - No `if (bits_in_cache < n)` underrun check (we verified upfront)
 *   - The refill still runs inside peek_unchecked (to load data from RAM)
 *
 * The caller guarantees preconditions:
 *   1. The stream has enough bits for at least one symbol + sentinel.
 *   2. Every peek requests exactly MAX_CODE_BITS (always > 0).
 *   3. The sentinel guarantees we stop before exhausting the stream.
 */

static void huffman_decode_fast(
    const uint8_t *data, size_t len,
    char *out, size_t out_size,
    int verbose)
{
    bs_reader_t br;
    bs_reader_init(&br, data, len);

    size_t pos = 0;
    while (pos < out_size - 1) {
        /* Precondition: at least MAX_CODE_BITS must be available.
         * In a real codec, the block header guarantees this. Here,
         * the sentinel ensures we always stop with bits to spare. */
        size_t remaining = bs_reader_bits_left(&br);
        assert(remaining >= MAX_CODE_BITS);

        /* ★ Fast-path: unchecked peek — no guards, no underrun check */
        uint32_t window = bs_peek_bits_unchecked(&br, MAX_CODE_BITS);

        const huff_entry_t *e = huff_match(window);
        assert(e != NULL);

        if (verbose) {
            printf("  [%3zu bits left] peek 0x%02X → ",
                   remaining, window);
            if (e->symbol == SENTINEL) {
                printf("sentinel (end)\n");
            } else {
                printf("'%c'  (code=%u, %u bits)\n",
                       e->symbol, e->code, e->bits);
            }
        }

        if (e->symbol == SENTINEL) {
            /* ★ Fast-path: unchecked skip */
            bs_skip_bits_unchecked(&br, e->bits);
            break;
        }

        out[pos++] = e->symbol;

        /* ★ Fast-path: unchecked skip — just decrements bits_in_cache */
        bs_skip_bits_unchecked(&br, e->bits);
    }
    out[pos] = '\0';
}

/* ── Demo ───────────────────────────────────────────────────────── */

static void print_hex(const char *label, const uint8_t *data, size_t len) {
    printf("%s", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

int main(void) {
    printf("fast_decode — Huffman: safe vs unchecked (fast-path)\n");
    printf("====================================================\n\n");

    const char *message = "the rat ate the tea";

    printf("Original: \"%s\" (%zu bytes ASCII)\n\n", message, strlen(message));

    /* ── Encode ── */
    uint8_t wire[64];
    size_t wire_len = huffman_encode(message, wire, sizeof(wire));

    printf("Encoded:  %zu bytes on wire\n", wire_len);
    print_hex("  Wire: ", wire, wire_len);
    printf("  Ratio:  %zu%% of original\n\n",
           (wire_len * 100) / strlen(message));

    /* ── Decode: safe ── */
    char decoded_safe[128];
    huffman_decode_safe(wire, wire_len, decoded_safe, sizeof(decoded_safe));

    printf("Decoded (safe):   \"%s\"\n", decoded_safe);

    /* ── Decode: fast-path with trace ── */
    char decoded_fast[128];
    printf("\nDecoding with unchecked fast-path:\n");
    huffman_decode_fast(wire, wire_len, decoded_fast, sizeof(decoded_fast), 1);

    printf("\nDecoded (fast):   \"%s\"\n\n", decoded_fast);

    /* ── Verify ── */
    assert(strcmp(decoded_safe, message) == 0);
    assert(strcmp(decoded_fast, message) == 0);
    assert(strcmp(decoded_safe, decoded_fast) == 0);

    printf("Verified: both decoders match the original message.\n");
    return 0;
}
