/*
 * deflate_block.c — Encode and decode a DEFLATE-style uncompressed block.
 *
 * Demonstrates: LSB-first read/write, byte alignment, and zero-copy
 * bulk byte transfer — the three features from Phase 4 working together
 * in a realistic scenario.
 *
 * A DEFLATE stored block (BTYPE=00) has this layout:
 *
 *   Bits   Order   Field
 *   ─────  ──────  ──────────────────────────────────────────────
 *     1    LSB     BFINAL  (1 = last block)
 *     2    LSB     BTYPE   (00 = no compression)
 *          ──────  ──── byte boundary ────
 *    16    LE      LEN     (number of literal bytes)
 *    16    LE      NLEN    (one's complement of LEN)
 *   LEN    bytes   literal data (copied verbatim)
 *   ─────
 *
 * After the 3-bit header, the format requires alignment to the next byte
 * boundary. LEN and NLEN are little-endian 16-bit words (byte-aligned),
 * and the literal data is a raw byte copy — exactly the use case for
 * bs_writer_align_lsb() + bs_write_bytes().
 *
 * Compile:
 *   gcc -std=c99 -Wall -Wextra -I. -o deflate_block deflate_block.c
 *
 * Run:
 *   ./deflate_block
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "bitstream.h"

/* ── Encoder ────────────────────────────────────────────────────── */

static size_t deflate_encode_stored_block(
    const uint8_t *literal, uint16_t len,
    int is_final,
    uint8_t *out, size_t out_size)
{
    bs_writer_t bw;
    bs_writer_init(&bw, out, out_size);

    /* ① 3-bit header (LSB-first, per DEFLATE spec) */
    bs_write_bits_lsb(&bw, is_final ? 1 : 0, 1);  /* BFINAL */
    bs_write_bits_lsb(&bw, 0, 2);                   /* BTYPE = 00 (stored) */

    /* ② Align to next byte boundary.
     * The 3 header bits occupy the low 3 bits of the first byte.
     * bs_writer_align_lsb pads the remaining 5 bits with zeros. */
    bs_writer_align_lsb(&bw);

    /* ③ LEN and NLEN as little-endian 16-bit words.
     * We write them as two 8-bit fields each (low byte first)
     * because LSB write order matches little-endian layout. */
    uint16_t nlen = (uint16_t)~len;
    bs_write_bits_lsb(&bw, (len >> 0) & 0xFF, 8);    /* LEN low */
    bs_write_bits_lsb(&bw, (len >> 8) & 0xFF, 8);    /* LEN high */
    bs_write_bits_lsb(&bw, (nlen >> 0) & 0xFF, 8);   /* NLEN low */
    bs_write_bits_lsb(&bw, (nlen >> 8) & 0xFF, 8);   /* NLEN high */

    /* ④ Bulk copy the literal data directly into the output buffer.
     * bs_write_bytes bypasses the bit accumulator entirely — it does
     * a straight memcpy, which is much faster for large payloads. */
    bool ok = bs_write_bytes(&bw, literal, len);
    assert(ok && "Output buffer too small for literal data");
    assert(!bw.overflow);

    return bs_writer_bytes_written(&bw);
}

/* ── Decoder ────────────────────────────────────────────────────── */

typedef struct {
    int      is_final;
    uint16_t len;
    uint8_t  data[256];
} stored_block_t;

static stored_block_t deflate_decode_stored_block(
    const uint8_t *in, size_t in_size)
{
    stored_block_t block;
    memset(&block, 0, sizeof(block));

    bs_reader_t br;
    bs_reader_init(&br, in, in_size);

    /* ① Read 3-bit header (LSB-first) */
    block.is_final = (int)bs_read_bits_lsb(&br, 1);  /* BFINAL */
    uint32_t btype = bs_read_bits_lsb(&br, 2);       /* BTYPE */
    assert(btype == 0 && "Expected BTYPE=00 (stored block)");
    (void)btype;

    /* ② Skip to byte boundary.
     * bs_reader_align_lsb discards the remaining 5 padding bits. */
    bs_reader_align_lsb(&br);

    /* ③ Read LEN and NLEN (little-endian 16-bit) */
    uint32_t len_lo  = bs_read_bits_lsb(&br, 8);
    uint32_t len_hi  = bs_read_bits_lsb(&br, 8);
    uint32_t nlen_lo = bs_read_bits_lsb(&br, 8);
    uint32_t nlen_hi = bs_read_bits_lsb(&br, 8);

    block.len = (uint16_t)(len_lo | (len_hi << 8));
    uint16_t nlen = (uint16_t)(nlen_lo | (nlen_hi << 8));

    /* Integrity check: NLEN must be one's complement of LEN */
    assert((uint16_t)(block.len ^ nlen) == 0xFFFF && "LEN/NLEN mismatch");
    (void)nlen;

    /* ④ Bulk copy literal data directly from the stream.
     * bs_read_bytes does a zero-copy memcpy from the source buffer. */
    bool ok = bs_read_bytes(&br, block.data, block.len);
    assert(ok && "Not enough data in stream");

    return block;
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
    printf("deflate_block — DEFLATE stored block (BTYPE=00)\n");
    printf("================================================\n\n");

    /* The literal payload */
    const char *message = "Hello, DEFLATE!";
    const uint8_t *payload = (const uint8_t *)message;
    uint16_t payload_len = (uint16_t)strlen(message);

    printf("Original:  \"%s\" (%u bytes)\n\n", message, payload_len);

    /* ── Encode ── */
    uint8_t wire[256];
    size_t wire_len = deflate_encode_stored_block(
        payload, payload_len, 1 /* is_final */, wire, sizeof(wire));

    printf("Encoded block: %zu bytes on wire\n", wire_len);
    print_hex("  Wire: ", wire, wire_len);

    /* Annotate the structure */
    printf("\n  Layout:\n");
    printf("    Byte 0:    0x%02X  ← header (BFINAL=1, BTYPE=00) + 5 padding bits\n", wire[0]);
    printf("    Bytes 1-2: 0x%02X %02X  ← LEN = %u (little-endian)\n",
           wire[1], wire[2], payload_len);
    printf("    Bytes 3-4: 0x%02X %02X  ← NLEN = 0x%04X (one's complement)\n",
           wire[3], wire[4], (uint16_t)~payload_len);
    printf("    Bytes 5-%zu: literal data\n\n", wire_len - 1);

    /* ── Decode ── */
    stored_block_t block = deflate_decode_stored_block(wire, wire_len);

    printf("Decoded:\n");
    printf("  BFINAL:  %d (last block: %s)\n", block.is_final,
           block.is_final ? "yes" : "no");
    printf("  LEN:     %u\n", block.len);
    printf("  Payload: \"%.*s\"\n\n", block.len, block.data);

    /* ── Verify ── */
    assert(block.is_final == 1);
    assert(block.len == payload_len);
    assert(memcmp(block.data, payload, payload_len) == 0);

    printf("Round-trip verified: decoded payload matches original.\n");
    return 0;
}
