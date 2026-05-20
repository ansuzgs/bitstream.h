/*
 * sensor_protocol.c — Encode and decode a compact sensor packet
 *
 * Simulates an IoT protocol where sensor readings are packed into
 * the minimum number of bits to save bandwidth:
 *
 *   [ 4] device_id          0..15
 *   [ 1] battery_low        flag
 *   [10] temperature        0..1023, maps to -40.0 .. +62.3 °C (0.1° steps)
 *   [ 7] humidity           0..100 %
 *   [ 1] alert              flag
 *   ---
 *   23 bits per reading -> 3 bytes (with 1 bit padding after flush)
 *
 * The example encodes several readings into a shared buffer,
 * then decodes and verifies them — a complete round-trip.
 *
 * Build:
 *   gcc -std=c99 -Wall -I.. -o sensor_protocol sensor_protocol.c
 *
 * Run:
 *   ./sensor_protocol
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bitstream.h"

#define BITS_PER_READING 23

typedef struct {
    uint8_t  device_id;     /* 4 bits */
    bool     battery_low;   /* 1 bit  */
    uint16_t temp_raw;      /* 10 bits: (temp_c + 40.0) * 10 */
    uint8_t  humidity;      /* 7 bits */
    bool     alert;         /* 1 bit  */
} sensor_reading_t;

/* Helpers: convert between raw 10-bit value and °C */
static uint16_t temp_to_raw(double celsius) {
    return (uint16_t)((celsius + 40.0) * 10.0);
}

static double raw_to_temp(uint16_t raw) {
    return (raw / 10.0) - 40.0;
}

/* ---- Encode a single reading -------------------------------------------- */

static void encode_reading(bs_writer_t *bw, const sensor_reading_t *r) {
    bs_write_bits(bw, r->device_id,   4);
    bs_write_bits(bw, r->battery_low,  1);
    bs_write_bits(bw, r->temp_raw,    10);
    bs_write_bits(bw, r->humidity,     7);
    bs_write_bits(bw, r->alert,        1);
}

/* ---- Decode a single reading -------------------------------------------- */

static sensor_reading_t decode_reading(bs_reader_t *br) {
    sensor_reading_t r;
    r.device_id   = (uint8_t)  bs_read_bits(br,  4);
    r.battery_low = (bool)     bs_read_bits(br,  1);
    r.temp_raw    = (uint16_t) bs_read_bits(br, 10);
    r.humidity    = (uint8_t)  bs_read_bits(br,  7);
    r.alert       = (bool)     bs_read_bits(br,  1);
    return r;
}

/* ---- Pretty-print ------------------------------------------------------- */

static void print_reading(int index, const sensor_reading_t *r) {
    printf("  [%d] device=%2u  bat=%s  temp=%+6.1f°C  hum=%3u%%  alert=%s\n",
           index,
           r->device_id,
           r->battery_low ? "LOW " : "ok  ",
           raw_to_temp(r->temp_raw),
           r->humidity,
           r->alert ? "YES" : "no");
}

/* ---- Main --------------------------------------------------------------- */

int main(void) {
    /* Sample data: 4 sensor readings */
    sensor_reading_t readings[] = {
        { .device_id = 1,  .battery_low = false,
          .temp_raw = temp_to_raw(22.5),  .humidity = 45, .alert = false },
        { .device_id = 2,  .battery_low = true,
          .temp_raw = temp_to_raw(-10.3), .humidity = 89, .alert = true  },
        { .device_id = 15, .battery_low = false,
          .temp_raw = temp_to_raw(0.0),   .humidity = 50, .alert = false },
        { .device_id = 7,  .battery_low = false,
          .temp_raw = temp_to_raw(55.8),  .humidity = 12, .alert = true  },
    };
    int count = (int)(sizeof(readings) / sizeof(readings[0]));

    /* --- Encode ---------------------------------------------------------- */

    uint8_t wire[32];
    memset(wire, 0, sizeof(wire));

    bs_writer_t bw;
    bs_writer_init(&bw, wire, sizeof(wire));

    for (int i = 0; i < count; i++) {
        encode_reading(&bw, &readings[i]);
    }
    bs_writer_flush(&bw);

    size_t bytes_on_wire = bs_writer_bytes_written(&bw);
    printf("Encoded %d readings into %zu bytes ", count, bytes_on_wire);
    printf("(%d bits of payload, %zu bits on wire)\n\n",
           count * BITS_PER_READING, bytes_on_wire * 8);

    /* Show the raw bytes */
    printf("Raw bytes: ");
    for (size_t i = 0; i < bytes_on_wire; i++) {
        printf("%02X ", wire[i]);
    }
    printf("\n\n");

    /* --- Decode ---------------------------------------------------------- */

    bs_reader_t br;
    bs_reader_init(&br, wire, bytes_on_wire);

    printf("Decoded readings:\n");
    for (int i = 0; i < count; i++) {
        sensor_reading_t r = decode_reading(&br);
        print_reading(i, &r);

        /* Verify round-trip */
        if (r.device_id   != readings[i].device_id   ||
            r.battery_low != readings[i].battery_low  ||
            r.temp_raw    != readings[i].temp_raw     ||
            r.humidity    != readings[i].humidity      ||
            r.alert       != readings[i].alert) {
            printf("  *** ROUND-TRIP MISMATCH at index %d ***\n", i);
            return 1;
        }
    }

    printf("\nRound-trip verified: all %d readings match.\n", count);
    return 0;
}
