/*
 * decode_mp3_header.c — Decode an MP3 frame header using bitstream.h
 *
 * An MP3 frame header is exactly 32 bits packed as:
 *
 *   [11] sync word (all 1s)
 *   [ 2] MPEG version       (00=2.5, 01=reserved, 10=2, 11=1)
 *   [ 2] layer              (01=III, 10=II, 11=I)
 *   [ 1] protection bit     (0=CRC follows header, 1=no CRC)
 *   [ 4] bitrate index
 *   [ 2] sample rate index  (00=44100, 01=48000, 10=32000 for MPEG1)
 *   [ 1] padding
 *   [ 1] private bit
 *   [ 2] channel mode       (00=stereo, 01=joint, 10=dual, 11=mono)
 *   [ 2] mode extension
 *   [ 1] copyright
 *   [ 1] original
 *   [ 2] emphasis
 *
 * This example parses a raw 4-byte header and prints every field.
 *
 * Build:
 *   gcc -std=c99 -Wall -I.. -o decode_mp3_header decode_mp3_header.c
 *
 * Run:
 *   ./decode_mp3_header
 */

#include <stdio.h>
#include <stdint.h>
#include "bitstream.h"

/* Lookup tables (MPEG1, Layer III only for brevity) */
static const int bitrate_table[] = {
    0, 32, 40, 48, 56, 64, 80, 96,
    112, 128, 160, 192, 224, 256, 320, -1
};

static const int samplerate_table[] = { 44100, 48000, 32000, -1 };

static const char *channel_modes[] = {
    "Stereo", "Joint Stereo", "Dual Channel", "Mono"
};

static const char *emphasis_modes[] = {
    "None", "50/15 ms", "reserved", "CCIT J.17"
};

int main(void) {
    /*
     * Example: a valid MPEG1 Layer III header.
     *
     * Binary breakdown:
     *   11111111 111_11_01_1  0100_01_0_0  01_00_0_1_00
     *   sync=0x7FF  v=3 l=1 p=1  br=4 sr=1 pad=0 priv=0  ch=1 ext=0 c=1 o=0 emp=0
     *
     * Hex: 0xFF FB 50 44
     */
    uint8_t raw_header[] = { 0xFF, 0xFB, 0x90, 0x44 };

    bs_reader_t bs;
    bs_reader_init(&bs, raw_header, sizeof(raw_header));

    /* Parse each field in order */
    uint32_t sync        = bs_read_bits(&bs, 11);
    uint32_t version     = bs_read_bits(&bs, 2);
    uint32_t layer       = bs_read_bits(&bs, 2);
    uint32_t protection  = bs_read_bits(&bs, 1);
    uint32_t bitrate_idx = bs_read_bits(&bs, 4);
    uint32_t srate_idx   = bs_read_bits(&bs, 2);
    uint32_t padding     = bs_read_bits(&bs, 1);
    uint32_t private_bit = bs_read_bits(&bs, 1);
    uint32_t channel     = bs_read_bits(&bs, 2);
    uint32_t mode_ext    = bs_read_bits(&bs, 2);
    uint32_t copyright   = bs_read_bits(&bs, 1);
    uint32_t original    = bs_read_bits(&bs, 1);
    uint32_t emphasis    = bs_read_bits(&bs, 2);

    /* Validate sync word */
    if (sync != 0x7FF) {
        printf("ERROR: invalid sync word 0x%03X (expected 0x7FF)\n", sync);
        return 1;
    }

    /* Print decoded fields */
    printf("MP3 Frame Header: %02X %02X %02X %02X\n",
           raw_header[0], raw_header[1], raw_header[2], raw_header[3]);
    printf("-----------------------------------\n");
    printf("  Sync word:     0x%03X (valid)\n", sync);
    printf("  MPEG version:  %s\n",
           version == 3 ? "MPEG1" : version == 2 ? "MPEG2" : "MPEG2.5");
    printf("  Layer:         %s\n",
           layer == 1 ? "Layer III" : layer == 2 ? "Layer II" : "Layer I");
    printf("  CRC protected: %s\n", protection ? "No" : "Yes");
    printf("  Bitrate:       %d kbps\n", bitrate_table[bitrate_idx]);
    printf("  Sample rate:   %d Hz\n", samplerate_table[srate_idx]);
    printf("  Padding:       %s\n", padding ? "Yes" : "No");
    printf("  Private bit:   %u\n", private_bit);
    printf("  Channel mode:  %s\n", channel_modes[channel]);
    printf("  Mode extension:%u\n", mode_ext);
    printf("  Copyright:     %s\n", copyright ? "Yes" : "No");
    printf("  Original:      %s\n", original ? "Yes" : "No");
    printf("  Emphasis:      %s\n", emphasis_modes[emphasis]);

    /* All 32 bits consumed */
    printf("\nBits remaining:  %zu (expected 0)\n",
           bs_reader_bits_left(&bs));

    return 0;
}
