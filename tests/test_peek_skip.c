/*
 * test_peek_skip.c — Phase 3: Peek & Skip
 *
 * Covers: bs_peek_bits (non-consuming read), bs_skip_bits (discard),
 *         zero-bit edge cases, cross-byte peek, insufficient data,
 *         peek+skip equivalence with read, Huffman-style lookahead.
 */
#include "bitstream.h"
#include "test.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */

/* Peek reads without consuming; subsequent read returns the same value. */
static void test_peek_basic(void) {
    uint8_t buffer[] = {0xA5}; /* 10100101 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_peek_bits(&bs, 4) == 10);    /* 1010 */
    assert(bs_reader_bits_left(&bs) == 8); /* nothing consumed */
    assert(bs_peek_bits(&bs, 4) == 10);    /* idempotent */
    assert(bs_read_bits(&bs, 4) == 10);    /* now consumes */
    assert(bs_reader_bits_left(&bs) == 4);
    assert(bs_peek_bits(&bs, 4) == 5); /* 0101 */
    assert(bs_read_bits(&bs, 4) == 5);
    assert(bs_reader_eof(&bs) == true);
}

/* Peek 0 bits: no-op, no UB even with a full 64-bit cache. */
static void test_peek_zero_bits(void) {
    uint8_t buffer[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 8);

    assert(bs_peek_bits(&bs, 0) == 0);
    assert(bs_reader_bits_left(&bs) == 64);
}

/* Peek a field that spans a byte boundary. */
static void test_peek_cross_byte(void) {
    uint8_t buffer[] = {0xAC, 0x35}; /* 10101100 00110101 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    bs_read_bits(&bs, 4);                   /* consume 1010 */
    assert(bs_peek_bits(&bs, 6) == 48);     /* 110000 = 48 */
    assert(bs_reader_bits_left(&bs) == 12); /* peek didn't consume */
    assert(bs_read_bits(&bs, 6) == 48);     /* now consumes */
}

/* Peek a full 32-bit word. */
static void test_peek_32_bits(void) {
    uint8_t buffer[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 5);

    assert(bs_peek_bits(&bs, 32) == 0x11223344);
    assert(bs_reader_bits_left(&bs) == 40); /* nothing consumed */
    assert(bs_read_bits(&bs, 32) == 0x11223344);
    assert(bs_read_bits(&bs, 8) == 0x55);
}

/* Peek requesting more bits than available returns 0, state unchanged. */
static void test_peek_insufficient_data(void) {
    uint8_t buffer[] = {0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    bs_read_bits(&bs, 6);                  /* 2 bits remaining */
    assert(bs_peek_bits(&bs, 4) == 0);     /* asks 4, has 2: returns 0 */
    assert(bs_reader_bits_left(&bs) == 2); /* state intact */
    assert(bs_read_bits(&bs, 2) == 3);     /* the 2 bits are still there */
}

/* Skip discards bits correctly. */
static void test_skip_basic(void) {
    uint8_t buffer[] = {0xA5, 0x3C}; /* 10100101 00111100 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    bs_skip_bits(&bs, 4); /* skip 1010 */
    assert(bs_reader_bits_left(&bs) == 12);
    assert(bs_read_bits(&bs, 4) == 5);  /* 0101 */
    bs_skip_bits(&bs, 4);               /* skip 0011 */
    assert(bs_read_bits(&bs, 4) == 12); /* 1100 */
    assert(bs_reader_eof(&bs) == true);
}

/* Skip 0 bits: no-op. */
static void test_skip_zero_bits(void) {
    uint8_t buffer[] = {0xAB};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    bs_skip_bits(&bs, 0);
    assert(bs_reader_bits_left(&bs) == 8);
    assert(bs_read_bits(&bs, 8) == 0xAB);
}

/* Skip past end: exhausts the stream without crashing. */
static void test_skip_past_end(void) {
    uint8_t buffer[] = {0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    bs_skip_bits(&bs, 100); /* asks more than available */
    assert(bs_reader_bits_left(&bs) == 0);
    assert(bs_reader_eof(&bs) == true);
}

/* peek(n) + skip(n) produces the same result as read(n). */
static void test_peek_skip_equals_read(void) {
    uint8_t buffer[] = {0xDE, 0xAD, 0xBE, 0xEF};
    bs_reader_t r1, r2;

    /* Stream 1: read */
    bs_reader_init(&r1, buffer, 4);
    uint32_t a1 = bs_read_bits(&r1, 12);
    uint32_t b1 = bs_read_bits(&r1, 20);

    /* Stream 2: peek+skip */
    bs_reader_init(&r2, buffer, 4);
    uint32_t a2 = bs_peek_bits(&r2, 12);
    bs_skip_bits(&r2, 12);
    uint32_t b2 = bs_peek_bits(&r2, 20);
    bs_skip_bits(&r2, 20);

    assert(a1 == a2);
    assert(b1 == b2);
    assert(bs_reader_bits_left(&r1) == bs_reader_bits_left(&r2));
}

/* Huffman-style lookahead: peek prefix to decide code length, then skip.
 * Code table: 0 -> 'A', 10 -> 'B', 11 -> 'C'
 * Encoded: A B C A C = 0 10 11 0 11 = 01011011 = 0x5B (8 bits exact) */
static void test_peek_skip_huffman_lookahead(void) {
    uint8_t buffer[1];
    memset(buffer, 0, sizeof(buffer));
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));
    bs_write_bits(&bw, 0, 1); /* A */
    bs_write_bits(&bw, 2, 2); /* B (10) */
    bs_write_bits(&bw, 3, 2); /* C (11) */
    bs_write_bits(&bw, 0, 1); /* A */
    bs_write_bits(&bw, 3, 2); /* C (11) */
    /* 1+2+2+1+2 = 8 bits, no flush needed */

    bs_reader_t br;
    bs_reader_init(&br, buffer, 1);

    char decoded[6];
    int idx = 0;

    while (!bs_reader_eof(&br) && idx < 5) {
        uint32_t prefix = bs_peek_bits(&br, 1);
        if (prefix == 0) {
            bs_skip_bits(&br, 1);
            decoded[idx++] = 'A';
        } else {
            uint32_t code = bs_peek_bits(&br, 2);
            bs_skip_bits(&br, 2);
            decoded[idx++] = (code == 2) ? 'B' : 'C';
        }
    }
    decoded[idx] = '\0';

    assert(strcmp(decoded, "ABCAC") == 0);
    assert(bs_reader_eof(&br) == true);
}

/* ------------------------------------------------------------------ */

void suite_peek_skip(void) {
    printf("=== Phase 3: Peek & Skip ===\n");
    RUN_TEST(test_peek_basic);
    RUN_TEST(test_peek_zero_bits);
    RUN_TEST(test_peek_cross_byte);
    RUN_TEST(test_peek_32_bits);
    RUN_TEST(test_peek_insufficient_data);
    RUN_TEST(test_skip_basic);
    RUN_TEST(test_skip_zero_bits);
    RUN_TEST(test_skip_past_end);
    RUN_TEST(test_peek_skip_equals_read);
    RUN_TEST(test_peek_skip_huffman_lookahead);
    printf("\n");
}
