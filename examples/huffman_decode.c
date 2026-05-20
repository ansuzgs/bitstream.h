/*
 * huffman_decode.c — Decode a Huffman-coded message using peek/skip
 *
 * Demonstrates bs_peek_bits() and bs_skip_bits() for variable-length
 * decoding: peek ahead to identify the code, then skip the consumed bits.
 *
 * This is the canonical use case for lookahead in bitstream processing.
 * Real-world codecs (DEFLATE, JPEG, MP3) use exactly this pattern.
 *
 * Code table (prefix-free):
 *
 *    Code    Bits  Symbol
 *   ──────  ─────  ──────
 *    0        1     'e'      (most frequent → shortest code)
 *    10       2     't'
 *    110      3     'a'
 *    1110     4     ' '      (space)
 *    11110    5     'h'
 *    111110   6     'r'
 *    111111   6     '\0'     (end-of-message sentinel)
 *
 * The encoder packs the message "the rat ate the tea" into a bitstream.
 * The decoder uses bs_peek_bits() to inspect up to 6 bits at a time,
 * matches the longest prefix, then bs_skip_bits() to advance.
 *
 * Build:
 *   gcc -std=c99 -Wall -I.. -o huffman_decode huffman_decode.c
 *
 * Run:
 *   ./huffman_decode
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "bitstream.h"

/* ---- Code table --------------------------------------------------------- */

#define MAX_CODE_LEN 6

typedef struct {
	uint8_t     code;       /* bit pattern, left-aligned in the byte */
	unsigned    bits;       /* code length */
	char        symbol;     /* decoded character ('\0' = end) */
} huffman_entry_t;

static const huffman_entry_t codebook[] = {
	/*  code    bits  symbol */
	{ 0x00,      1,   'e'  },   /* 0       */
	{ 0x02,      2,   't'  },   /* 10      */
	{ 0x06,      3,   'a'  },   /* 110     */
	{ 0x0E,      4,   ' '  },   /* 1110    */
	{ 0x1E,      5,   'h'  },   /* 11110   */
	{ 0x3E,      6,   'r'  },   /* 111110  */
	{ 0x3F,      6,   '\0' },   /* 111111  (sentinel) */
};

static const int codebook_size = (int)(sizeof(codebook) / sizeof(codebook[0]));

/* ---- Encoder: symbol → bits --------------------------------------------- */

static bool encode_symbol(bs_writer_t *bw, char ch) {
	for (int i = 0; i < codebook_size; i++) {
		if (codebook[i].symbol == ch) {
			bs_write_bits(bw, codebook[i].code, codebook[i].bits);
			return true;
		}
	}
	return false; /* symbol not in codebook */
}

/* ---- Decoder: peek to identify code, skip to consume -------------------- */

static char decode_symbol(bs_reader_t *br) {
	/*
	 * Peek MAX_CODE_LEN bits.  Compare against each codebook entry
	 * by masking the peeked window down to the entry's length.
	 *
	 * This is O(n) for clarity; a real codec would use a lookup table
	 * indexed by the peeked value for O(1) decode.
	 */
	uint32_t window = bs_peek_bits(br, MAX_CODE_LEN);

	for (int i = 0; i < codebook_size; i++) {
		unsigned len = codebook[i].bits;
		/* Extract the top 'len' bits of the 6-bit window */
		uint32_t candidate = window >> (MAX_CODE_LEN - len);

		if (candidate == codebook[i].code) {
			bs_skip_bits(br, len);
			return codebook[i].symbol;
		}
	}

	/* Should never reach here with a valid stream */
	fprintf(stderr, "ERROR: unrecognized code (window=0x%02X)\n", window);
	bs_skip_bits(br, 1); /* skip 1 bit and hope to resync */
	return '?';
}

/* ---- Main --------------------------------------------------------------- */

int main(void) {
	const char *message = "the rat ate the tea";

	/* ---- Encode --------------------------------------------------------- */

	uint8_t wire[64];
	memset(wire, 0, sizeof(wire));

	bs_writer_t bw;
	bs_writer_init(&bw, wire, sizeof(wire));

	printf("Encoding: \"%s\"\n\n", message);

	for (const char *p = message; *p; p++) {
		if (!encode_symbol(&bw, *p)) {
			printf("ERROR: '%c' not in codebook\n", *p);
			return 1;
		}
	}
	encode_symbol(&bw, '\0'); /* end-of-message sentinel */
	bs_writer_flush(&bw);

	size_t bytes = bs_writer_bytes_written(&bw);
	size_t msg_len = strlen(message);

	printf("  Original:  %zu bytes (ASCII)\n", msg_len);
	printf("  Encoded:   %zu bytes on wire\n", bytes);
	printf("  Ratio:     %.0f%%\n", 100.0 * (double)bytes / (double)msg_len);
	printf("\n");

	/* Show raw bytes */
	printf("  Raw bytes: ");
	for (size_t i = 0; i < bytes; i++) {
		printf("%02X ", wire[i]);
	}
	printf("\n\n");

	/* ---- Decode --------------------------------------------------------- */

	bs_reader_t br;
	bs_reader_init(&br, wire, bytes);

	char decoded[256];
	int idx = 0;

	printf("Decoding with peek/skip:\n");

	while (!bs_reader_eof(&br) && idx < 255) {
		/* Show the state before each decode step */
		size_t bits_left = bs_reader_bits_left(&br);

		char ch = decode_symbol(&br);

		if (ch == '\0') {
			printf("  [%3zu bits left] peek → sentinel (end)\n", bits_left);
			break;
		}

		printf("  [%3zu bits left] peek → '%c'\n", bits_left, ch);
		decoded[idx++] = ch;
	}
	decoded[idx] = '\0';

	printf("\n");
	printf("Decoded: \"%s\"\n", decoded);

	/* ---- Verify --------------------------------------------------------- */

	if (strcmp(decoded, message) == 0) {
		printf("\nRound-trip verified: output matches original.\n");
		return 0;
	} else {
		printf("\n*** MISMATCH ***\n");
		printf("Expected: \"%s\"\n", message);
		printf("Got:      \"%s\"\n", decoded);
		return 1;
	}
}
