# Examples

Complete, self-contained programs that demonstrate `bitstream.h` in realistic scenarios. Each one compiles independently and runs without external data.

```
make examples
```

## decode_mp3_header.c

Parses a raw 4-byte MP3 frame header and prints every field. Demonstrates **read-only** usage with a real-world binary format.

An MP3 frame header is exactly 32 bits, packed MSB-first:

```
 Bits  Field              Values (MPEG1 Layer III)
─────  ─────────────────  ──────────────────────────────────
  11   Sync word          0x7FF (all ones)
   2   MPEG version       11=MPEG1, 10=MPEG2, 00=MPEG2.5
   2   Layer              01=III, 10=II, 11=I
   1   Protection         1=no CRC, 0=CRC follows header
   4   Bitrate index      → lookup table (32–320 kbps)
   2   Sample rate index  00=44100, 01=48000, 10=32000 Hz
   1   Padding            1=frame is padded
   1   Private bit
   2   Channel mode       00=stereo, 01=joint, 10=dual, 11=mono
   2   Mode extension
   1   Copyright
   1   Original
   2   Emphasis           00=none, 01=50/15ms, 11=CCIT J.17
─────
  32   total
```

Sample output:

```
MP3 Frame Header: FF FB 90 44
-----------------------------------
  Sync word:     0x7FF (valid)
  MPEG version:  MPEG1
  Layer:         Layer III
  CRC protected: No
  Bitrate:       128 kbps
  Sample rate:   44100 Hz
  Padding:       No
  Channel mode:  Joint Stereo
  ...
```

## sensor_protocol.c

Encodes and decodes a compact IoT sensor packet. Demonstrates the full **write → read round-trip**: define a struct, pack it into the minimum number of bits, then unpack and verify.

Each sensor reading is 23 bits:

```
 Bits  Field         Range
─────  ────────────  ──────────────────────────────────
   4   Device ID     0–15
   1   Battery low   flag
  10   Temperature   0–1023 → maps to -40.0 … +62.3 °C
   7   Humidity      0–100 %
   1   Alert         flag
─────
  23   total         → 3 bytes per reading after flush
```

Four readings (92 bits of payload) pack into 12 bytes on the wire. The program encodes them, prints the raw hex, decodes them back, and verifies every field matches the original.

Sample output:

```
Encoded 4 readings into 12 bytes (92 bits of payload, 96 bits on wire)

Raw bytes: 14 E2 B4 54 A6 CF CC 83 23 BB E1 90

Decoded readings:
  [0] device= 1  bat=ok    temp= +22.5°C  hum= 45%  alert=no
  [1] device= 2  bat=LOW   temp= -10.3°C  hum= 89%  alert=YES
  [2] device=15  bat=ok    temp=  +0.0°C  hum= 50%  alert=no
  [3] device= 7  bat=ok    temp= +55.8°C  hum= 12%  alert=YES

Round-trip verified: all 4 readings match.
```
