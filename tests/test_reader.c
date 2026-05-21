/*
 * test_reader.c — Phase 1: Reader (MSB-First)
 *
 * Covers: init, single-bit reads, multi-bit reads, cross-byte boundary,
 *         32-bit reads, bits_left tracking, EOF detection, zero-bit reads.
 */
#include "bitstream.h"
#include "test.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */

static void test_init_and_state(void) {
    uint8_t buffer[] = {0xFF, 0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    assert(bs_reader_bits_left(&bs) == 16);
    assert(bs_reader_eof(&bs) == false);
}

static void test_known_answer(void) {
    /* 0xA5 = 10100101 */
    uint8_t buffer[] = {0xA5};
    bs_reader_t reader;
    bs_reader_init(&reader, buffer, 1);

    assert(bs_read_bits(&reader, 4) == 10); /* 1010 */
    assert(bs_read_bits(&reader, 4) == 5);  /* 0101 */
}

static void test_read_single_bits(void) {
    uint8_t buffer[] = {0xAC}; /* 10101100 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_read_bit(&bs) == 1);
    assert(bs_read_bit(&bs) == 0);
    assert(bs_read_bit(&bs) == 1);
    assert(bs_read_bit(&bs) == 0);
    assert(bs_read_bit(&bs) == 1);
    assert(bs_read_bit(&bs) == 1);
    assert(bs_read_bit(&bs) == 0);
    assert(bs_read_bit(&bs) == 0);

    assert(bs_reader_eof(&bs) == true);
    assert(bs_reader_bits_left(&bs) == 0);
}

static void test_read_cross_boundary(void) {
    uint8_t buffer[] = {0xAC, 0x35}; /* 10101100 00110101 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    assert(bs_read_bits(&bs, 4) == 10);
    assert(bs_read_bits(&bs, 6) == 48);
    assert(bs_read_bits(&bs, 6) == 53);
    assert(bs_reader_eof(&bs) == true);
}

static void test_read_32_bits(void) {
    uint8_t buffer[] = {0x11, 0x22, 0x33, 0x44};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 4);

    assert(bs_read_bits(&bs, 32) == 0x11223344);
    assert(bs_reader_eof(&bs) == true);
}

static void test_bits_left_tracking(void) {
    uint8_t buffer[] = {0xAA, 0xBB, 0xCC};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 3);

    assert(bs_reader_bits_left(&bs) == 24);

    bs_read_bits(&bs, 5);
    assert(bs_reader_bits_left(&bs) == 19);

    bs_read_bits(&bs, 15);
    assert(bs_reader_bits_left(&bs) == 4);

    bs_read_bits(&bs, 4);
    assert(bs_reader_bits_left(&bs) == 0);
}

static void test_read_zero_bits(void) {
    uint8_t buffer[] = {0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_read_bits(&bs, 0) == 0);
    assert(bs_reader_bits_left(&bs) == 8);
}

/* ------------------------------------------------------------------ */

void suite_reader(void) {
    printf("=== Phase 1: Reader ===\n");
    RUN_TEST(test_init_and_state);
    RUN_TEST(test_known_answer);
    RUN_TEST(test_read_single_bits);
    RUN_TEST(test_read_cross_boundary);
    RUN_TEST(test_read_32_bits);
    RUN_TEST(test_bits_left_tracking);
    RUN_TEST(test_read_zero_bits);
    printf("\n");
}
