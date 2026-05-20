/*
 * test_regression.c — Bug regression tests
 *
 * Each test documents a specific bug, its root cause, and the fix.
 * These must never be removed — they guard against regressions.
 *
 * Bug #1: Cache overflow when n > 57 in bs_write_bits
 * Bug #2: Undefined Behavior on right-shift by 64 when n == 0
 * Bug #3: Silent output buffer overflow (no error flag)
 * Bug #4: bits_in_cache underflow wrapping size_t to huge value
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "test.h"
#include "bitstream.h"

#ifdef _WIN32
  #define HAS_FORK 0
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #define HAS_FORK 1
#endif


#if HAS_FORK
static int expect_abort(void (*fn)(void)) {
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == -1) return -1;
	if (pid == 0) {
		/* Hijo: ejecutar la funcion que deberia abortar */
		fn();
		_exit(0); /* Si llega aqui, NO aborto */
	}
	/* Padre: esperar al hijo */
	int status;
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		/* SIGABRT (assert) o SIGSEGV (crash) cuentan como "aborto esperado" */
		return (sig == 6 || sig == 11) ? 1 : 0;
	}
	return 0; /* Salio limpio: la funcion NO aborto */
}
#endif

/* ------------------------------------------------------------------ */
/* BUG #1: Cache overflow when n > 57 in bs_write_bits.
 *
 * If bits_in_cache == 7 (typical residue after a dump) and n == 58:
 *   bit_cache = (bit_cache << 58)  =>  destroys the 7 existing bits.
 *   bits_in_cache = 65 > 64: the 64-bit accumulator overflows.
 *
 * Tests bug1_large_n and bug1_n57_boundary are commented out in main()
 * because the fix (assert n <= 32) would abort. They exist as documentation.
 * ------------------------------------------------------------------ */

/* Callback para expect_abort: intenta bs_write_bits con n=33 */
#if HAS_FORK && !defined(NDEBUG)
static void _bug1_write_n33(void) {
	uint8_t buf[16];
	bs_writer_t bw;
	bs_writer_init(&bw, buf, sizeof(buf));
	bs_write_bits(&bw, 0, 33); /* n=33: debe disparar assert */
}
#endif

/* Verificar que n > 32 dispara el assert (solo en debug builds) */
void test_bug1_assert_fires_on_n_gt_32(void) {
#if !HAS_FORK
	printf("SKIP (fork no disponible en esta plataforma)\n");
#elif defined(NDEBUG)
	printf("SKIP (asserts desactivados en release)\n");
#else
	int r = expect_abort(_bug1_write_n33);
	if (r == 1) { printf("OK)\n"); }
	else if (r == 0) { printf("FALLO: no se disparo el assert\n"); assert(false); }
	else { printf("SKIP (fork() fallo)\n"); }
#endif
}

/* n=32 es el limite maximo permitido: debe funcionar sin problemas.
 * Con 7 bits residuales en cache: 7+32=39 < 64, sin desbordamiento. */
void test_bug1_n32_is_safe_limit(void) {
	uint8_t buffer[16];
	memset(buffer, 0, sizeof(buffer));
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, sizeof(buffer));

	/* Crear 7 bits residuales en cache: escribir 15 bits, dump vuelca 1 byte */
	bs_write_bits(&bw, 0x7FFF, 15);
	/* Ahora bits_in_cache == 7, escribir 32 bits mas: 7+32 = 39 < 64 */
	bs_write_bits(&bw, 0xDEADBEEF, 32);
	bs_writer_flush(&bw);

	bs_reader_t br;
	bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));

	uint32_t v1 = bs_read_bits(&br, 15);
	uint32_t v2 = bs_read_bits(&br, 32);

	if (v1 != 0x7FFF) {
		printf("\nFALLO: cabecera corrompida. Esperado 0x7FFF, obtenido 0x%X\n", v1);
		assert(false);
	}
	if (v2 != 0xDEADBEEF) {
		printf("\nFALLO: payload corrompido. Esperado 0xDEADBEEF, obtenido 0x%X\n", v2);
		assert(false);
	}

	printf("OK\n");
}


static void test_bug1_consecutive_32bit_writes(void) {
	uint8_t buffer[32];
	memset(buffer, 0, sizeof(buffer));
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, sizeof(buffer));

	uint32_t values[] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xAABBCCDD };
	int count = (int)(sizeof(values) / sizeof(values[0]));

	for (int i = 0; i < count; i++) {
		bs_write_bits(&bw, values[i], 32);
	}
	bs_writer_flush(&bw);

	bs_reader_t br;
	bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
	for (int i = 0; i < count; i++) {
		uint32_t v = bs_read_bits(&br, 32);
		if (v != values[i]) {
			printf("\n  FAIL at [%d]: expected 0x%X, got 0x%X\n",
			       i, values[i], v);
			assert(0);
		}
	}
}

/* ------------------------------------------------------------------ */
/* BUG #2: UB in bs_read_bits when n == 0 and cache is full (64 bits).
 *
 * _bs_reader_refill can fill cache to exactly 64 bits. With n == 0:
 *   (bit_cache >> 64) is undefined behavior in C99.
 *
 * Fix: early return `if (n == 0) return 0;` at top of bs_read_bits.
 * ------------------------------------------------------------------ */

static void test_bug2_read_zero_bits_full_cache(void) {
	uint8_t buffer[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
	bs_reader_t bs;
	bs_reader_init(&bs, buffer, 8);

	uint32_t result = bs_read_bits(&bs, 0);

	assert(result == 0);
	assert(bs_reader_bits_left(&bs) == 64);
	assert(bs_read_bits(&bs, 8) == 0x01);
}

static void test_bug2_zero_bits_anywhere_in_stream(void) {
	uint8_t buffer[] = { 0xAB, 0xCD };
	bs_reader_t bs;
	bs_reader_init(&bs, buffer, 2);

	assert(bs_read_bits(&bs, 0) == 0);
	assert(bs_reader_bits_left(&bs) == 16);

	assert(bs_read_bits(&bs, 8) == 0xAB);
	assert(bs_read_bits(&bs, 0) == 0);
	assert(bs_reader_bits_left(&bs) == 8);

	assert(bs_read_bits(&bs, 8) == 0xCD);
	assert(bs_read_bits(&bs, 0) == 0);
	assert(bs_reader_eof(&bs) == true);
}

static void test_bug2_write_zero_bits_is_noop(void) {
	uint8_t buffer[4] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 4);

	bs_write_bits(&bw, 0xFF, 8);
	bs_write_bits(&bw, 0x00, 0); /* no-op */
	bs_write_bits(&bw, 0x42, 8);
	bs_writer_flush(&bw);

	assert(bs_writer_bytes_written(&bw) == 2);
	assert(buffer[0] == 0xFF);
	assert(buffer[1] == 0x42);
}

/* ------------------------------------------------------------------ */
/* BUG #3: Silent output buffer overflow.
 *
 * When the buffer fills, _bs_writer_dump breaks the loop and sets
 * the overflow flag. Previously there was no flag at all.
 * These tests verify that already-written bytes are not corrupted
 * and that the overflow flag is set.
 * ------------------------------------------------------------------ */

static void test_bug3_overflow_preserves_written_bytes(void) {
	uint8_t buffer[2] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 2);

	bs_write_bits(&bw, 0xAA, 8);
	bs_write_bits(&bw, 0xBB, 8);
	bs_write_bits(&bw, 0xCC, 8); /* doesn't fit: silently dropped */

	assert(bs_writer_bytes_written(&bw) == 2);
	assert(buffer[0] == 0xAA);
	assert(buffer[1] == 0xBB);
	assert(bw.overflow == true);
}

static void test_bug3_overflow_mid_field_no_corruption(void) {
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0xAB, 8);  /* fills the buffer */
	bs_write_bits(&bw, 0xF, 4);   /* doesn't fit */
	bs_write_bits(&bw, 0xF, 4);   /* doesn't fit either */
	bs_writer_flush(&bw);

	assert(buffer[0] == 0xAB);
	assert(bs_writer_bytes_written(&bw) == 1);
}

/* ------------------------------------------------------------------ */
/* BUG #4: bits_in_cache as signed int can produce huge size_t.
 *
 * If a bug left bits_in_cache negative, the implicit cast to size_t
 * in bs_reader_bits_left would return ~18446744073709551615.
 * This test verifies bits_left never exceeds the initial total.
 * ------------------------------------------------------------------ */

static void test_bug4_bits_left_never_wraps_around(void) {
	uint8_t buffer[8];
	memset(buffer, 0xAA, sizeof(buffer));
	bs_reader_t bs;
	bs_reader_init(&bs, buffer, 8);

	assert(bs_reader_bits_left(&bs) == 64);

	bs_read_bits(&bs, 1);
	size_t left = bs_reader_bits_left(&bs);
	assert(left == 63);
	assert(left < 10000); /* guard: negative-to-size_t would be enormous */

	bs_read_bits(&bs, 31);
	bs_read_bits(&bs, 31);
	assert(bs_reader_bits_left(&bs) == 1);

	bs_read_bits(&bs, 1);
	left = bs_reader_bits_left(&bs);
	assert(left == 0);
	assert(left < 10000);
	assert(bs_reader_eof(&bs) == true);
}

/* ------------------------------------------------------------------ */

void suite_regression(void) {
	printf("=== Regression: Known Bugs ===\n");
	RUN_TEST(test_bug1_assert_fires_on_n_gt_32);
	RUN_TEST(test_bug1_n32_is_safe_limit);
	RUN_TEST(test_bug1_consecutive_32bit_writes);
	RUN_TEST(test_bug2_read_zero_bits_full_cache);
	RUN_TEST(test_bug2_zero_bits_anywhere_in_stream);
	RUN_TEST(test_bug2_write_zero_bits_is_noop);
	RUN_TEST(test_bug3_overflow_preserves_written_bytes);
	RUN_TEST(test_bug3_overflow_mid_field_no_corruption);
	RUN_TEST(test_bug4_bits_left_never_wraps_around);
	printf("\n");
}
