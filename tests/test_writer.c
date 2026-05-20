/*
 * test_writer.c — Phase 2: Writer (MSB-First)
 *
 * Covers: basic byte assembly, flush/padding, dirty value masking,
 *         mirror round-trip (write then read back).
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "test.h"
#include "bitstream.h"

/* ------------------------------------------------------------------ */

static void test_writer_basic_byte(void) {
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0xA, 4);
	bs_write_bits(&bw, 0x5, 4);

	assert(bs_writer_bytes_written(&bw) == 1);
	assert(buffer[0] == 0xA5);
}

static void test_writer_flush_padding(void) {
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0x5, 3);

	assert(bs_writer_bytes_written(&bw) == 0);

	bs_writer_flush(&bw);
	assert(bs_writer_bytes_written(&bw) == 1);
	assert(buffer[0] == 0xA0);
}

static void test_writer_dirty_values(void) {
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	/* Dirty value 0xFF, but only 2 bits stored (binary 11) */
	bs_write_bits(&bw, 0xFF, 2);
	/* Dirty value 0xAA00, but only 6 bits stored (binary 000000) */
	bs_write_bits(&bw, 0xAA00, 6);

	/* 11 + 000000 = 11000000 = 0xC0 */
	assert(buffer[0] == 0xC0);
}

static void test_mirror_round_trip(void) {
	uint32_t input_values[] = { 1, 5, 23, 0, 1023, 12, 1, 15, 2048, 3 };
	unsigned int input_lengths[]     = { 1, 3, 5,  2, 10,   4,  1, 4,  12,   2 };
	int num_elements = (int)(sizeof(input_lengths) / sizeof(input_lengths[0]));

	/* Total bits = 1+3+5+2+10+4+1+4+12+2 = 44 -> 6 bytes with padding */
	uint8_t ram_buffer[16] = { 0 };

	/* Write */
	bs_writer_t bw;
	bs_writer_init(&bw, ram_buffer, sizeof(ram_buffer));
	for (int i = 0; i < num_elements; i++) {
		bs_write_bits(&bw, input_values[i], input_lengths[i]);
	}
	bs_writer_flush(&bw);

	size_t total_bytes = bs_writer_bytes_written(&bw);
	assert(total_bytes == 6);

	/* Read back */
	bs_reader_t br;
	bs_reader_init(&br, ram_buffer, total_bytes);
	assert(bs_reader_bits_left(&br) == 48); /* 44 data + 4 padding */

	for (int i = 0; i < num_elements; i++) {
		uint32_t v = bs_read_bits(&br, input_lengths[i]);
		if (v != input_values[i]) {
			printf("\n  FAIL at index %d: expected %u, got %u\n",
			       i, input_values[i], v);
			assert(0);
		}
	}

	/* Remaining bits must be the 4 zero-padding bits */
	assert(bs_reader_bits_left(&br) == 4);
	assert(bs_read_bits(&br, 4) == 0);
	assert(bs_reader_eof(&br) == true);
}

/* ------------------------------------------------------------------ */

void suite_writer(void) {
	printf("=== Phase 2: Writer ===\n");
	RUN_TEST(test_writer_basic_byte);
	RUN_TEST(test_writer_flush_padding);
	RUN_TEST(test_writer_dirty_values);
	RUN_TEST(test_mirror_round_trip);
	printf("\n");
}
