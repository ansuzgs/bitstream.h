/*
 * test_lsb.c — Phase 5: LSB-First Read/Write/Peek/Skip
 *
 * Covers: bs_read_bits_lsb, bs_write_bits_lsb, bs_peek_bits_lsb,
 *         bs_skip_bits_lsb, bs_writer_flush_lsb, round-trips,
 *         zero-bit edge cases, cross-byte boundaries, dirty values,
 *         overflow, and DEFLATE-style variable-length decoding.
 */
#include "bitstream.h"
#include "test.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ================================================================== */
/*  Reader LSB                                                        */
/* ================================================================== */

/* LSB reads extract bits from the least-significant end first.
 * 0xA5 = binary 10100101.
 * LSB order: first 4 bits = 0101 = 5, next 4 bits = 1010 = 10.
 * (Contrast with MSB: first 4 = 1010 = 10, next 4 = 0101 = 5.) */
static void test_lsb_read_known_answer(void) {
    uint8_t buffer[] = {0xA5}; /* 10100101 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_read_bits_lsb(&bs, 4) == 5);  /* bits 0-3: 0101 */
    assert(bs_read_bits_lsb(&bs, 4) == 10); /* bits 4-7: 1010 */
    assert(bs_reader_eof(&bs) == true);
}

/* Read single bits in LSB order. 0xB3 = 10110011.
 * LSB first: 1,1,0,0,1,1,0,1 */
static void test_lsb_read_single_bits(void) {
    uint8_t buffer[] = {0xB3}; /* 10110011 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_read_bits_lsb(&bs, 1) == 1);
    assert(bs_read_bits_lsb(&bs, 1) == 1);
    assert(bs_read_bits_lsb(&bs, 1) == 0);
    assert(bs_read_bits_lsb(&bs, 1) == 0);
    assert(bs_read_bits_lsb(&bs, 1) == 1);
    assert(bs_read_bits_lsb(&bs, 1) == 1);
    assert(bs_read_bits_lsb(&bs, 1) == 0);
    assert(bs_read_bits_lsb(&bs, 1) == 1);
    assert(bs_reader_eof(&bs) == true);
}

/* Cross-byte boundary: 6 bits from first byte + 2 bits from second.
 * Buffer: 0xAC=10101100  0x35=00110101
 * LSB refill puts 0xAC in bits[0:7] and 0x35 in bits[8:15].
 * Read 6 → bits[0:5] of 0xAC = 101100 = 0x2C = 44
 * Read 6 → bits[6:11] = top2(0xAC)|bot4(0x35) = 10|0101 = 100101 = 0x25 = 37
 * Read 4 → bits[12:15] = top4(0x35) = 0011 = 3 */
static void test_lsb_read_cross_boundary(void) {
    uint8_t buffer[] = {0xAC, 0x35};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    assert(bs_read_bits_lsb(&bs, 6) == 44); /* 0b101100 */
    assert(bs_read_bits_lsb(&bs, 6) == 22); /* 0b010110 */
    assert(bs_read_bits_lsb(&bs, 4) == 3);  /* 0b0011   */
    assert(bs_reader_eof(&bs) == true);
}

/* Full 32-bit read LSB. Bytes 0x11,0x22,0x33,0x44 in memory.
 * LSB refill: 0x11 at bits[0:7], 0x22 at [8:15], 0x33 at [16:23], 0x44 at [24:31].
 * 32-bit value = 0x44332211. */
static void test_lsb_read_32_bits(void) {
    uint8_t buffer[] = {0x11, 0x22, 0x33, 0x44};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 4);

    assert(bs_read_bits_lsb(&bs, 32) == 0x44332211);
    assert(bs_reader_eof(&bs) == true);
}

/* Read 0 bits: no-op, no UB. */
static void test_lsb_read_zero_bits(void) {
    uint8_t buffer[] = {0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_read_bits_lsb(&bs, 0) == 0);
    assert(bs_reader_bits_left(&bs) == 8);
}

/* bits_left tracking through LSB reads. */
static void test_lsb_read_bits_left_tracking(void) {
    uint8_t buffer[] = {0xAA, 0xBB, 0xCC};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 3);

    assert(bs_reader_bits_left(&bs) == 24);
    bs_read_bits_lsb(&bs, 5);
    assert(bs_reader_bits_left(&bs) == 19);
    bs_read_bits_lsb(&bs, 15);
    assert(bs_reader_bits_left(&bs) == 4);
    bs_read_bits_lsb(&bs, 4);
    assert(bs_reader_bits_left(&bs) == 0);
    assert(bs_reader_eof(&bs) == true);
}

/* ================================================================== */
/*  Writer LSB                                                        */
/* ================================================================== */

/* Write 4+4 bits LSB. Writing 0x5 (0101) then 0xA (1010) in LSB:
 * 0x5 fills bits[0:3]=0101, 0xA fills bits[4:7]=1010
 * Byte emitted: 10100101 = 0xA5 */
static void test_lsb_writer_basic_byte(void) {
    uint8_t buffer[1] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 1);

    bs_write_bits_lsb(&bw, 0x5, 4);
    bs_write_bits_lsb(&bw, 0xA, 4);

    assert(bs_writer_bytes_written(&bw) == 1);
    assert(buffer[0] == 0xA5);
}

/* Flush pads remaining bits with zeros on the MSB side.
 * Write 3 bits: value 5 = 101.  bits[0:2] = 101.
 * Flush pads bits[3:7] with 0 → 00000101 = 0x05. */
static void test_lsb_writer_flush_padding(void) {
    uint8_t buffer[1] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 1);

    bs_write_bits_lsb(&bw, 5, 3); /* 101 */
    assert(bs_writer_bytes_written(&bw) == 0);

    bs_writer_flush_lsb(&bw);
    assert(bs_writer_bytes_written(&bw) == 1);
    assert(buffer[0] == 0x05);
}

/* Dirty value masking: only the low n bits should be stored.
 * Write 2 bits of 0xFF → only 0b11.  Then 6 bits of 0xAA00 → 0b000000.
 * Result: bits[0:1]=11, bits[2:7]=000000 → 00000011 = 0x03. */
static void test_lsb_writer_dirty_values(void) {
    uint8_t buffer[1] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 1);

    bs_write_bits_lsb(&bw, 0xFF, 2);   /* stores 11 */
    bs_write_bits_lsb(&bw, 0xAA00, 6); /* stores 000000 */

    assert(buffer[0] == 0x03);
}

/* Write 0 bits: no-op. */
static void test_lsb_write_zero_bits(void) {
    uint8_t buffer[2] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 2);

    bs_write_bits_lsb(&bw, 0xAB, 8);
    bs_write_bits_lsb(&bw, 0xFF, 0); /* no-op */
    bs_write_bits_lsb(&bw, 0xCD, 8);

    assert(bs_writer_bytes_written(&bw) == 2);
    assert(buffer[0] == 0xAB);
    assert(buffer[1] == 0xCD);
}

/* Overflow detection: written bytes are preserved, flag is set. */
static void test_lsb_overflow_preserves_bytes(void) {
    uint8_t buffer[2] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 2);

    bs_write_bits_lsb(&bw, 0xAA, 8);
    bs_write_bits_lsb(&bw, 0xBB, 8);
    bs_write_bits_lsb(&bw, 0xCC, 8); /* no cabe */

    assert(bs_writer_bytes_written(&bw) == 2);
    assert(buffer[0] == 0xAA);
    assert(buffer[1] == 0xBB);
    assert(bw.overflow == true);
}

/* ================================================================== */
/*  Peek & Skip LSB                                                   */
/* ================================================================== */

/* Peek reads without consuming. */
static void test_lsb_peek_basic(void) {
    uint8_t buffer[] = {0xA5}; /* LSB first: low nibble=5, high nibble=10 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    assert(bs_peek_bits_lsb(&bs, 4) == 5);
    assert(bs_reader_bits_left(&bs) == 8); /* nothing consumed */
    assert(bs_peek_bits_lsb(&bs, 4) == 5); /* idempotent */
    assert(bs_read_bits_lsb(&bs, 4) == 5); /* now consumes */
    assert(bs_reader_bits_left(&bs) == 4);
    assert(bs_peek_bits_lsb(&bs, 4) == 10);
    assert(bs_read_bits_lsb(&bs, 4) == 10);
    assert(bs_reader_eof(&bs) == true);
}

/* Peek 0 bits: no-op. */
static void test_lsb_peek_zero_bits(void) {
    uint8_t buffer[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 8);

    assert(bs_peek_bits_lsb(&bs, 0) == 0);
    assert(bs_reader_bits_left(&bs) == 64);
}

/* Peek across byte boundary. */
static void test_lsb_peek_cross_byte(void) {
    uint8_t buffer[] = {0xAC, 0x35};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    bs_read_bits_lsb(&bs, 4); /* consume low nibble of 0xAC (0x0C=1100) */
    /* remaining: bits [4:7] of 0xAC = 1010, then 0x35 = 00110101
     * peek 6 → bits [4:9] = 1010 | 01 from 0x35 = 010_1010 → no...
     *
     * After reading 4 bits, cache has shifted right by 4.
     * Cache content (LSB refill): 0x35AC >> 4 low bits consumed.
     * Actually: refill loads 0xAC at [0:7], 0x35 at [8:15] → 0x35AC.
     * After reading 4: cache = 0x35AC >> 4 = 0x035A, bits_in_cache=12.
     * Peek 6: low 6 bits of 0x35A = 0x1A = 26. */
    assert(bs_peek_bits_lsb(&bs, 6) == 0x1A); /* 011010 = 26 */
    assert(bs_reader_bits_left(&bs) == 12);   /* peek didn't consume */
    assert(bs_read_bits_lsb(&bs, 6) == 0x1A); /* now consumes */
}

/* Peek full 32 bits. */
static void test_lsb_peek_32_bits(void) {
    uint8_t buffer[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 5);

    assert(bs_peek_bits_lsb(&bs, 32) == 0x44332211);
    assert(bs_reader_bits_left(&bs) == 40); /* nothing consumed */
    assert(bs_read_bits_lsb(&bs, 32) == 0x44332211);
    assert(bs_read_bits_lsb(&bs, 8) == 0x55);
}

/* Peek more bits than available: returns 0, state unchanged. */
static void test_lsb_peek_insufficient_data(void) {
    uint8_t buffer[] = {0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    bs_read_bits_lsb(&bs, 6);
    assert(bs_peek_bits_lsb(&bs, 4) == 0); /* asks 4, has 2 */
    assert(bs_reader_bits_left(&bs) == 2);
    assert(bs_read_bits_lsb(&bs, 2) == 3); /* the 2 bits are still there */
}

/* Skip discards bits. */
static void test_lsb_skip_basic(void) {
    uint8_t buffer[] = {0xA5, 0x3C};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    bs_skip_bits_lsb(&bs, 4); /* skip low nibble of 0xA5 (5) */
    assert(bs_reader_bits_left(&bs) == 12);
    assert(bs_read_bits_lsb(&bs, 4) == 0x0A); /* high nibble of 0xA5 */
    bs_skip_bits_lsb(&bs, 4);                 /* skip low nibble of 0x3C */
    assert(bs_read_bits_lsb(&bs, 4) == 0x03); /* high nibble of 0x3C */
    assert(bs_reader_eof(&bs) == true);
}

/* Skip 0 bits: no-op. */
static void test_lsb_skip_zero_bits(void) {
    uint8_t buffer[] = {0xAB};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    bs_skip_bits_lsb(&bs, 0);
    assert(bs_reader_bits_left(&bs) == 8);
    assert(bs_read_bits_lsb(&bs, 8) == 0xAB);
}

/* Skip past end: exhausts without crashing. */
static void test_lsb_skip_past_end(void) {
    uint8_t buffer[] = {0xFF};
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 1);

    bs_skip_bits_lsb(&bs, 100);
    assert(bs_reader_bits_left(&bs) == 0);
    assert(bs_reader_eof(&bs) == true);
}

/* peek(n) + skip(n) == read(n) */
static void test_lsb_peek_skip_equals_read(void) {
    uint8_t buffer[] = {0xDE, 0xAD, 0xBE, 0xEF};
    bs_reader_t r1, r2;

    bs_reader_init(&r1, buffer, 4);
    uint32_t a1 = bs_read_bits_lsb(&r1, 12);
    uint32_t b1 = bs_read_bits_lsb(&r1, 20);

    bs_reader_init(&r2, buffer, 4);
    uint32_t a2 = bs_peek_bits_lsb(&r2, 12);
    bs_skip_bits_lsb(&r2, 12);
    uint32_t b2 = bs_peek_bits_lsb(&r2, 20);
    bs_skip_bits_lsb(&r2, 20);

    assert(a1 == a2);
    assert(b1 == b2);
    assert(bs_reader_bits_left(&r1) == bs_reader_bits_left(&r2));
}

/* ================================================================== */
/*  Round-trip LSB                                                    */
/* ================================================================== */

/* Write variable-width fields in LSB mode, read them back. */
static void test_lsb_mirror_round_trip(void) {
    uint32_t values[] = {1, 5, 23, 0, 1023, 12, 1, 15, 2048, 3};
    unsigned int bits[] = {1, 3, 5, 2, 10, 4, 1, 4, 12, 2};
    int count = (int)(sizeof(bits) / sizeof(bits[0]));
    /* Total: 1+3+5+2+10+4+1+4+12+2 = 44 bits → 6 bytes with padding */

    uint8_t buffer[16] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));

    for (int i = 0; i < count; i++) {
        bs_write_bits_lsb(&bw, values[i], bits[i]);
    }
    bs_writer_flush_lsb(&bw);
    assert(bs_writer_bytes_written(&bw) == 6);

    bs_reader_t br;
    bs_reader_init(&br, buffer, 6);

    for (int i = 0; i < count; i++) {
        uint32_t v = bs_read_bits_lsb(&br, bits[i]);
        if (v != values[i]) {
            printf("\n  FAIL at index %d: expected %u, got %u\n", i, values[i], v);
            assert(0);
        }
    }

    /* Remaining 4 padding bits should be zero */
    assert(bs_reader_bits_left(&br) == 4);
    assert(bs_read_bits_lsb(&br, 4) == 0);
    assert(bs_reader_eof(&br) == true);
}

/* Consecutive 32-bit writes in LSB mode. */
static void test_lsb_consecutive_32bit_writes(void) {
    uint8_t buffer[32] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));

    uint32_t values[] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xAABBCCDD};
    int count = (int)(sizeof(values) / sizeof(values[0]));

    for (int i = 0; i < count; i++) {
        bs_write_bits_lsb(&bw, values[i], 32);
    }
    bs_writer_flush_lsb(&bw);

    bs_reader_t br;
    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
    for (int i = 0; i < count; i++) {
        uint32_t v = bs_read_bits_lsb(&br, 32);
        if (v != values[i]) {
            printf("\n  FAIL at [%d]: expected 0x%X, got 0x%X\n", i, values[i], v);
            assert(0);
        }
    }
}

/* 0xFFFFFFFF aligned and misaligned, LSB mode. */
static void test_lsb_max_uint32(void) {
    uint8_t buffer[8] = {0};

    /* Aligned */
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));
    bs_write_bits_lsb(&bw, 0xFFFFFFFF, 32);
    bs_writer_flush_lsb(&bw);

    bs_reader_t br;
    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
    assert(bs_read_bits_lsb(&br, 32) == 0xFFFFFFFF);

    /* Unaligned: 1-bit prefix + 32 bits + 1-bit suffix */
    memset(buffer, 0, sizeof(buffer));
    bs_writer_init(&bw, buffer, sizeof(buffer));
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 0xFFFFFFFF, 32);
    bs_write_bits_lsb(&bw, 0, 1);
    bs_writer_flush_lsb(&bw);

    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
    assert(bs_read_bits_lsb(&br, 1) == 1);
    assert(bs_read_bits_lsb(&br, 32) == 0xFFFFFFFF);
    assert(bs_read_bits_lsb(&br, 1) == 0);
}

/* ================================================================== */
/*  MSB vs LSB: verify they produce different byte layouts             */
/* ================================================================== */

/* Same logical value (0x05, 3 bits) produces different bytes in
 * MSB vs LSB mode due to different bit ordering. */
static void test_msb_lsb_differ(void) {
    uint8_t msb_buf[1] = {0};
    uint8_t lsb_buf[1] = {0};

    bs_writer_t bw_msb, bw_lsb;

    bs_writer_init(&bw_msb, msb_buf, 1);
    bs_write_bits(&bw_msb, 5, 3); /* MSB: 101_____ → flush → 10100000 = 0xA0 */
    bs_writer_flush(&bw_msb);

    bs_writer_init(&bw_lsb, lsb_buf, 1);
    bs_write_bits_lsb(&bw_lsb, 5, 3); /* LSB: _____101 → flush → 00000101 = 0x05 */
    bs_writer_flush_lsb(&bw_lsb);

    assert(msb_buf[0] == 0xA0);
    assert(lsb_buf[0] == 0x05);
    assert(msb_buf[0] != lsb_buf[0]); /* different layout */
}

/* ================================================================== */
/*  DEFLATE-style variable-length decoding (LSB peek+skip)            */
/* ================================================================== */

/* Simplified DEFLATE-like variable-length decoding (LSB peek+skip).
 *
 * Prefix-free code table (reading bits LSB-first from the stream):
 *   bit pattern   symbol
 *   0             'A'
 *   10            'B'
 *   110           'C'
 *   1110          'D'
 *   1111          'E'
 *
 * Message: "ABCDE"
 * Bit stream (in order of extraction): 0 | 10 | 110 | 1110 | 1111
 * Total: 1+2+3+4+4 = 14 bits.
 *
 * We write each bit individually with bs_write_bits_lsb(1 bit) to
 * guarantee exact stream layout. */
static void test_lsb_deflate_style_decode(void) {
    uint8_t buffer[4] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));

    /* A: 0 */
    bs_write_bits_lsb(&bw, 0, 1);
    /* B: 1,0 */
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 0, 1);
    /* C: 1,1,0 */
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 0, 1);
    /* D: 1,1,1,0 */
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 0, 1);
    /* E: 1,1,1,1 */
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 1, 1);
    bs_write_bits_lsb(&bw, 1, 1);
    /* 14 bits total */
    bs_writer_flush_lsb(&bw);

    bs_reader_t br;
    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));

    char decoded[6];
    int idx = 0;

    while (idx < 5) {
        uint32_t b = bs_peek_bits_lsb(&br, 1);
        if (b == 0) {
            bs_skip_bits_lsb(&br, 1);
            decoded[idx++] = 'A';
        } else {
            /* Peek progressively longer windows to find code */
            uint32_t peek;
            /* Peek up to 4 bits to find the code */
            peek = bs_peek_bits_lsb(&br, 2);
            if ((peek & 0x2) == 0) { /* x0 → B */
                bs_skip_bits_lsb(&br, 2);
                decoded[idx++] = 'B';
            } else {
                peek = bs_peek_bits_lsb(&br, 3);
                if ((peek & 0x4) == 0) { /* xx0 → C */
                    bs_skip_bits_lsb(&br, 3);
                    decoded[idx++] = 'C';
                } else {
                    peek = bs_peek_bits_lsb(&br, 4);
                    bs_skip_bits_lsb(&br, 4);
                    if ((peek & 0x8) == 0) {
                        decoded[idx++] = 'D';
                    } else {
                        decoded[idx++] = 'E';
                    }
                }
            }
        }
    }
    decoded[idx] = '\0';

    assert(strcmp(decoded, "ABCDE") == 0);
}

/* ================================================================== */

void suite_lsb(void) {
    printf("=== Phase 5: LSB-First ===\n");

    /* Reader */
    RUN_TEST(test_lsb_read_known_answer);
    RUN_TEST(test_lsb_read_single_bits);
    RUN_TEST(test_lsb_read_cross_boundary);
    RUN_TEST(test_lsb_read_32_bits);
    RUN_TEST(test_lsb_read_zero_bits);
    RUN_TEST(test_lsb_read_bits_left_tracking);

    /* Writer */
    RUN_TEST(test_lsb_writer_basic_byte);
    RUN_TEST(test_lsb_writer_flush_padding);
    RUN_TEST(test_lsb_writer_dirty_values);
    RUN_TEST(test_lsb_write_zero_bits);
    RUN_TEST(test_lsb_overflow_preserves_bytes);

    /* Peek & Skip */
    RUN_TEST(test_lsb_peek_basic);
    RUN_TEST(test_lsb_peek_zero_bits);
    RUN_TEST(test_lsb_peek_cross_byte);
    RUN_TEST(test_lsb_peek_32_bits);
    RUN_TEST(test_lsb_peek_insufficient_data);
    RUN_TEST(test_lsb_skip_basic);
    RUN_TEST(test_lsb_skip_zero_bits);
    RUN_TEST(test_lsb_skip_past_end);
    RUN_TEST(test_lsb_peek_skip_equals_read);

    /* Round-trip */
    RUN_TEST(test_lsb_mirror_round_trip);
    RUN_TEST(test_lsb_consecutive_32bit_writes);
    RUN_TEST(test_lsb_max_uint32);

    /* MSB vs LSB divergence */
    RUN_TEST(test_msb_lsb_differ);

    /* DEFLATE-style decode */
    RUN_TEST(test_lsb_deflate_style_decode);

    printf("\n");
}
