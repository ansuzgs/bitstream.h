/*
 * test_phase4.c — Phase 4: Unchecked Variants, Alignment, and Bulk Transfers
 *
 * Covers: Fast-path unchecked reads/writes (MSB/LSB), reader/writer
 * byte alignment (MSB/LSB), and zero-copy bulk byte operations.
 */
#include "bitstream.h"
#include "test.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ================================================================== */
/* Phase 4A: Unchecked Variants (MSB)                                */
/* ================================================================== */

/* Unchecked variants assume preconditions are met. We pre-verify with
 * checked methods or ensure buffer capacity to avoid Undefined Behavior. */
static void test_msb_unchecked_read_round_trip(void) {
    uint8_t buffer[] = {0x8A, 0x5F}; /* 10001010  01011111 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    /* Precondition check: we have 16 bits left, safe to use unchecked */
    assert(bs_reader_bits_left(&bs) >= 16);

    assert(bs_peek_bits_unchecked(&bs, 4) == 8);   /* 1000 */
    assert(bs_read_bits_unchecked(&bs, 4) == 8);   /* Consume 1000 */

    assert(bs_peek_bits_unchecked(&bs, 6) == 41);  /* 101001 = 41 */
    bs_skip_bits_unchecked(&bs, 6);                 /* Discard 101001 */

    assert(bs_read_bits_unchecked(&bs, 6) == 31);  /* Remaining 11111 = 31 */
    assert(bs_reader_eof(&bs) == true);
}

static void test_msb_unchecked_writer(void) {
    uint8_t buffer[2] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 2);

    /* Precondition: Values must be clean (no high bits set) and space must exist */
    bs_write_bits_unchecked(&bw, 0x5, 4);  /* 0b0101 */
    bs_write_bits_unchecked(&bw, 0x9, 4);  /* 0b1001 */
    bs_write_bits_unchecked(&bw, 0xFF, 8); /* 0b11111111 */

    bs_writer_flush(&bw);
    assert(bs_writer_bytes_written(&bw) == 2);
    assert(buffer[0] == 0x59);
    assert(buffer[1] == 0xFF);
}

/* ================================================================== */
/* Phase 4A: Unchecked Variants (LSB)                                */
/* ================================================================== */

static void test_lsb_unchecked_read_round_trip(void) {
    uint8_t buffer[] = {0xA5, 0x3C}; /* LSB order: 5 then 10, then 12 then 3 */
    bs_reader_t bs;
    bs_reader_init(&bs, buffer, 2);

    assert(bs_reader_bits_left(&bs) >= 16);

    assert(bs_peek_bits_lsb_unchecked(&bs, 4) == 5);
    assert(bs_read_bits_lsb_unchecked(&bs, 4) == 5);

    bs_skip_bits_lsb_unchecked(&bs, 4); /* skips 10 */

    assert(bs_read_bits_lsb_unchecked(&bs, 8) == 0x3C);
    assert(bs_reader_eof(&bs) == true);
}

static void test_lsb_unchecked_writer(void) {
    uint8_t buffer[2] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 2);

    bs_write_bits_lsb_unchecked(&bw, 0x5, 4);  /* bits[0:3] = 0101 */
    bs_write_bits_lsb_unchecked(&bw, 0xA, 4);  /* bits[4:7] = 1010 */
    bs_write_bits_lsb_unchecked(&bw, 0x3C, 8);

    bs_writer_flush_lsb(&bw);
    assert(buffer[0] == 0xA5);
    assert(buffer[1] == 0x3C);
}

/* ================================================================== */
/* Phase 4B: Byte Alignment (MSB & LSB)                              */
/* ================================================================== */

static void test_msb_alignment(void) {
    uint8_t buffer[4] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));

    /* Test writer alignment */
    bs_write_bits(&bw, 5, 3); /* 101_____ */
    bs_writer_align(&bw);     /* Pads with 0s -> 10100000 = 0xA0 */

    /* If already aligned, it should be a no-op */
    bs_writer_align(&bw);

    bs_write_bits(&bw, 0xFF, 8);
    bs_writer_flush(&bw);

    assert(buffer[0] == 0xA0);
    assert(buffer[1] == 0xFF);

    /* Test reader alignment */
    bs_reader_t br;
    bs_reader_init(&br, buffer, 2);

    assert(bs_read_bits(&br, 3) == 5);
    bs_reader_align(&br); /* Discards remaining 5 bits of byte 0 */

    /* If already aligned, should be a no-op */
    bs_reader_align(&br);

    assert(bs_read_bits(&br, 8) == 0xFF);
    assert(bs_reader_eof(&br) == true);
}

static void test_lsb_alignment(void) {
    uint8_t buffer[2] = {0};
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));

    bs_write_bits_lsb(&bw, 5, 3); /* _____101 */
    bs_writer_align_lsb(&bw);     /* Pads MSB with 0s -> 00000101 = 0x05 */
    bs_write_bits_lsb(&bw, 0xAA, 8);
    bs_writer_flush_lsb(&bw);

    assert(buffer[0] == 0x05);
    assert(buffer[1] == 0xAA);

    bs_reader_t br;
    bs_reader_init(&br, buffer, 2);
    assert(bs_read_bits_lsb(&br, 3) == 5);
    bs_reader_align_lsb(&br);
    assert(bs_read_bits_lsb(&br, 8) == 0xAA);
}

/* ================================================================== */
/* Phase 4C: Bulk Byte Transfer                                      */
/* ================================================================== */

static void test_bulk_bytes_transfer_success(void) {
    uint8_t src_payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t write_buffer[8] = {0};

    bs_writer_t bw;
    bs_writer_init(&bw, write_buffer, sizeof(write_buffer));

    /* Write some preamble bits and align before bulk transfer */
    bs_write_bits(&bw, 0x7, 3);
    bs_writer_align(&bw);

    /* Bulk copy direct from buffer */
    bool write_ok = bs_write_bytes(&bw, src_payload, 4);
    assert(write_ok == true);
    bs_writer_flush(&bw);

    /* Verify layout: [Preamble + Padding (1 byte)] + [4 bytes payload] */
    assert(bs_writer_bytes_written(&bw) == 5);
    assert(write_buffer[0] == 0xE0); /* 0b11100000 */
    assert(memcmp(&write_buffer[1], src_payload, 4) == 0);

    /* Reader side */
    bs_reader_t br;
    uint8_t dst_payload[4] = {0};
    bs_reader_init(&br, write_buffer, 5);

    assert(bs_read_bits(&br, 3) == 0x7);
    bs_reader_align(&br);

    /* Bulk copy directly into destination */
    bool read_ok = bs_read_bytes(&br, dst_payload, 4);
    assert(read_ok == true);
    assert(memcmp(dst_payload, src_payload, 4) == 0);
    assert(bs_reader_eof(&br) == true);
}

static void test_bulk_bytes_alignment_guards(void) {
    uint8_t payload[] = {0x11, 0x22};
    uint8_t buffer[4] = {0};

    /* Writer alignment guard */
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, sizeof(buffer));
    bs_write_bits(&bw, 1, 1);
    /* Precondition violation check: calling bulk write without alignment must return false */
    assert(bs_write_bytes(&bw, payload, 2) == false);

    /* Reader alignment guard */
    uint8_t data[] = {0x00, 0x11, 0x22};
    bs_reader_t br;
    bs_reader_init(&br, data, 3);
    bs_read_bits(&br, 1);
    /* Precondition violation check: calling bulk read without alignment must return false */
    assert(bs_read_bytes(&br, buffer, 2) == false);
}

static void test_bulk_bytes_overflow_underrun(void) {
    uint8_t payload[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t buffer[3] = {0};

    /* Writer capacity constraint */
    bs_writer_t bw;
    bs_writer_init(&bw, buffer, 3);
    assert(bs_write_bytes(&bw, payload, 5) == false); /* Requesting 5 bytes on a 3-byte buffer */
    assert(bw.overflow == true);

    /* Reader underrun constraint */
    bs_reader_t br;
    bs_reader_init(&br, buffer, 3);
    assert(bs_read_bytes(&br, payload, 5) == false); /* Asking for more bytes than stream has left */
}

/* ================================================================== */

void suite_phase4(void) {
    printf("=== Phase 4: Fast-Paths, Alignment & Bulk ===\n");

    /* Phase 4A: Unchecked MSB */
    RUN_TEST(test_msb_unchecked_read_round_trip);
    RUN_TEST(test_msb_unchecked_writer);

    /* Phase 4A: Unchecked LSB */
    RUN_TEST(test_lsb_unchecked_read_round_trip);
    RUN_TEST(test_lsb_unchecked_writer);

    /* Phase 4B: Alignment */
    RUN_TEST(test_msb_alignment);
    RUN_TEST(test_lsb_alignment);

    /* Phase 4C: Bulk Byte Transfers */
    RUN_TEST(test_bulk_bytes_transfer_success);
    RUN_TEST(test_bulk_bytes_alignment_guards);
    RUN_TEST(test_bulk_bytes_overflow_underrun);

    printf("\n");
}
