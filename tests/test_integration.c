/*
 * test_integration.c — End-to-end scenarios
 *
 * These tests simulate real-world usage patterns: variable-length
 * Huffman-like streams, max uint32 values aligned and unaligned, etc.
 */
#include "bitstream.h"
#include "test.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */

/* Simulates a Huffman-like codec stream: variable-length fields
 * including a misaligned 32-bit word. */
static void test_huffman_like_stream(void) {
    typedef struct {
        uint32_t value;
        unsigned int bits;
    } field_t;
    field_t fields[] = {
        {1, 1}, {0, 1},  {7, 3},     {0xDEAD, 16}, {0xFFFFFFFF, 32}, /* 32 bits misaligned */
        {0, 1}, {31, 5}, {0x1FF, 9}, {3, 2},
    };
    /* Total: 1+1+3+16+32+1+5+9+2 = 70 bits -> 9 bytes + 2 padding bits */
    int count = (int)(sizeof(fields) / sizeof(fields[0]));

    uint8_t buffer[16];
    memset(buffer, 0, sizeof(buffer));
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));

    for (int i = 0; i < count; i++) {
        bs_write_bits(&bw, fields[i].value, fields[i].bits);
    }
    bs_writer_flush(&bw);
    assert(bs_writer_bytes_written(&bw) == 9);

    bs_reader_t br;
    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
    for (int i = 0; i < count; i++) {
        uint32_t v = bs_read_bits(&br, fields[i].bits);
        if (v != fields[i].value) {
            printf("\n  FAIL at field %d (%d bits): expected 0x%X, got 0x%X\n", i, fields[i].bits,
                   fields[i].value, v);
            assert(0);
        }
    }
    assert(bs_read_bits(&br, 2) == 0); /* padding */
    assert(bs_reader_eof(&br) == true);
}

/* 0xFFFFFFFF aligned and misaligned (1-bit header). */
static void test_max_uint32_aligned_and_unaligned(void) {
    uint8_t buffer[8];

    /* Aligned */
    memset(buffer, 0, sizeof(buffer));
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));
    bs_write_bits(&bw, 0xFFFFFFFF, 32);
    bs_writer_flush(&bw);

    bs_reader_t br;
    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
    assert(bs_read_bits(&br, 32) == 0xFFFFFFFF);

    /* Unaligned: 1-bit prefix + 32 bits + 1-bit suffix */
    memset(buffer, 0, sizeof(buffer));
    bs_writer_init(&bw, buffer, sizeof(buffer));
    bs_write_bits(&bw, 1, 1);
    bs_write_bits(&bw, 0xFFFFFFFF, 32);
    bs_write_bits(&bw, 0, 1);
    bs_writer_flush(&bw);

    bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
    assert(bs_read_bits(&br, 1) == 1);
    assert(bs_read_bits(&br, 32) == 0xFFFFFFFF);
    assert(bs_read_bits(&br, 1) == 0);
}

/* ------------------------------------------------------------------ */

void suite_integration(void) {
    printf("=== Integration: End-to-End ===\n");
    RUN_TEST(test_huffman_like_stream);
    RUN_TEST(test_max_uint32_aligned_and_unaligned);
    printf("\n");
}
