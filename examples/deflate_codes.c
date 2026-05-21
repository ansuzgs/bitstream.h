/*
 * deflate_codes.c — LSB-first variable-length coding (DEFLATE style)
 *
 * Demonstrates the LSB-first API by encoding a short message with a
 * prefix-free Huffman code and decoding it with peek/skip lookahead,
 * exactly as real DEFLATE (gzip, zlib, PNG) works internally.
 *
 * Key difference from MSB-first (MP3, JPEG):
 *   MSB packs bits from the top of each byte   → first bit in bit 7.
 *   LSB packs bits from the bottom of each byte → first bit in bit 0.
 *   DEFLATE, ZIP, and PNG all use LSB-first ordering.
 *
 * Compile:  gcc -std=c99 -I.. -O2 -o deflate_codes deflate_codes.c
 * Run:      ./deflate_codes
 */

#include "bitstream.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Code table (prefix-free, designed for LSB-first extraction):       */
/*                                                                    */
/*   Symbol   Code (binary, LSB first)   Length                       */
/*   ──────   ────────────────────────   ──────                       */
/*   'e'      0                          1                            */
/*   't'      01                         2                            */
/*   'a'      011                        3                            */
/*   ' '      0111                       4                            */
/*   'h'      01111                      5                            */
/*   'r'      011111                     6                            */
/*   EOS      111111                     6   (end-of-stream sentinel) */
/*                                                                    */
/* Reading LSB-first: if bit 0 is 0, it's 'e'. If bit 0 is 1, read   */
/* bit 1: if 0, it's 't'. If 1, read bit 2: if 0, it's 'a'. Etc.    */
/* This cascading structure is how DEFLATE decodes in practice.       */
/* ------------------------------------------------------------------ */

typedef struct {
    char symbol;         /* decoded character ('\0' = EOS) */
    uint32_t code;       /* bit pattern (LSB-first)        */
    unsigned int length; /* number of bits                  */
} code_entry_t;

static const code_entry_t CODE_TABLE[] = {
    {'e', 0x00, 1},  /* 0         */
    {'t', 0x01, 2},  /* 10        */
    {'a', 0x03, 3},  /* 110       */
    {' ', 0x07, 4},  /* 1110      */
    {'h', 0x0F, 5},  /* 11110     */
    {'r', 0x1F, 6},  /* 111110    */
    {'\0', 0x3F, 6}, /* 111111    (EOS sentinel) */
};
static const int NUM_CODES = (int)(sizeof(CODE_TABLE) / sizeof(CODE_TABLE[0]));

/* Max code length in the table */
#define MAX_CODE_LEN 6

/* ------------------------------------------------------------------ */
/* Encoder: look up each character and write its code LSB-first.      */
/* ------------------------------------------------------------------ */

static size_t encode(const char *message, uint8_t *out, size_t out_size) {
    bs_writer_t bw;
    bs_writer_init(&bw, out, out_size);

    for (const char *p = message; *p != '\0'; p++) {
        bool found = false;
        for (int i = 0; i < NUM_CODES; i++) {
            if (CODE_TABLE[i].symbol == *p) {
                bs_write_bits_lsb(&bw, CODE_TABLE[i].code, CODE_TABLE[i].length);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("  ERROR: '%c' not in code table\n", *p);
            return 0;
        }
    }

    /* Write EOS sentinel */
    bs_write_bits_lsb(&bw, 0x3F, 6);
    bs_writer_flush_lsb(&bw);

    if (bw.overflow) {
        printf("  ERROR: output buffer too small\n");
        return 0;
    }

    return bs_writer_bytes_written(&bw);
}

/* ------------------------------------------------------------------ */
/* Decoder: peek bits, match against the code table, then skip.       */
/*                                                                    */
/* This uses the cascading-bit approach: peek 1 bit at a time,        */
/* extending the window until we find a matching code. This is the    */
/* naive approach; real DEFLATE uses a lookup table for speed, but     */
/* the bit-level logic is identical.                                  */
/* ------------------------------------------------------------------ */

static void decode(const uint8_t *data, size_t size, char *out, size_t out_size) {
    bs_reader_t br;
    bs_reader_init(&br, data, size);

    size_t idx = 0;

    while (idx < out_size - 1) {
        /* Peek the maximum code length and match */
        unsigned int available = (unsigned int)bs_reader_bits_left(&br);
        if (available == 0) break;

        unsigned int peek_len = (available < MAX_CODE_LEN) ? available : MAX_CODE_LEN;
        uint32_t window = bs_peek_bits_lsb(&br, peek_len);

        printf("  [%2zu bits left] peek %u bits = 0x%02X → ", bs_reader_bits_left(&br), peek_len,
               window);

        bool matched = false;
        for (int i = 0; i < NUM_CODES; i++) {
            uint32_t mask = (1U << CODE_TABLE[i].length) - 1;
            if (CODE_TABLE[i].length <= peek_len && (window & mask) == CODE_TABLE[i].code) {
                bs_skip_bits_lsb(&br, CODE_TABLE[i].length);

                if (CODE_TABLE[i].symbol == '\0') {
                    printf("EOS (end-of-stream)\n");
                    out[idx] = '\0';
                    return;
                }

                printf("'%c' (%u bits)\n", CODE_TABLE[i].symbol, CODE_TABLE[i].length);
                out[idx++] = CODE_TABLE[i].symbol;
                matched = true;
                break;
            }
        }

        if (!matched) {
            printf("ERROR: no matching code\n");
            break;
        }
    }

    out[idx] = '\0';
}

/* ------------------------------------------------------------------ */
/* Main: encode, print hex dump, decode, verify round-trip.           */
/* ------------------------------------------------------------------ */

int main(void) {
    const char *message = "the rat ate the tea";

    printf("DEFLATE-style LSB-first coding demo\n");
    printf("====================================\n\n");
    printf("Message: \"%s\"\n\n", message);

    /* Print code table */
    printf("Code table (LSB-first, prefix-free):\n");
    printf("  Symbol   Code        Bits\n");
    printf("  ──────   ─────────   ────\n");
    for (int i = 0; i < NUM_CODES; i++) {
        char sym_str[8];
        if (CODE_TABLE[i].symbol == ' ')
            snprintf(sym_str, sizeof(sym_str), "SPACE");
        else if (CODE_TABLE[i].symbol == '\0')
            snprintf(sym_str, sizeof(sym_str), "EOS");
        else
            snprintf(sym_str, sizeof(sym_str), "'%c'", CODE_TABLE[i].symbol);

        /* Print code bits in transmission order (LSB first) */
        char bits_str[8];
        for (unsigned int b = 0; b < CODE_TABLE[i].length; b++) {
            bits_str[b] = ((CODE_TABLE[i].code >> b) & 1) ? '1' : '0';
        }
        bits_str[CODE_TABLE[i].length] = '\0';

        printf("  %-7s  %-10s  %u\n", sym_str, bits_str, CODE_TABLE[i].length);
    }
    printf("\n");

    /* Encode */
    uint8_t wire[64];
    size_t wire_bytes = encode(message, wire, sizeof(wire));
    size_t original_bytes = strlen(message);

    printf("Encoding:\n");
    printf("  Original:  %zu bytes (ASCII)\n", original_bytes);
    printf("  Encoded:   %zu bytes on wire\n", wire_bytes);
    printf("  Ratio:     %zu%%\n\n", (wire_bytes * 100) / original_bytes);

    printf("  Raw bytes:");
    for (size_t i = 0; i < wire_bytes; i++) {
        printf(" %02X", wire[i]);
    }
    printf("\n\n");

    /* Decode */
    printf("Decoding (peek/skip, LSB-first):\n");
    char decoded[256];
    decode(wire, wire_bytes, decoded, sizeof(decoded));

    printf("\n");
    printf("Decoded: \"%s\"\n\n", decoded);

    /* Verify */
    if (strcmp(message, decoded) == 0) {
        printf("Round-trip verified: output matches original.\n");
    } else {
        printf("MISMATCH!\n");
        printf("  Expected: \"%s\"\n", message);
        printf("  Got:      \"%s\"\n", decoded);
        return 1;
    }

    return 0;
}
