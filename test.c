#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
  #define HAS_FORK 0
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #define HAS_FORK 1
#endif

#include "bitstream.h"

/* ========================================================================
 * Utilidad: verificar que una funcion aborta (dispara assert).
 * Usa fork() para ejecutar en un subproceso sin matar al padre.
 * Solo disponible en sistemas POSIX. En Windows se salta el test.
 * ======================================================================== */
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

/* ========================================================================
 * FASE 1: TESTS DEL LECTOR (MSB-First)
 * ======================================================================== */

void test_fase1_kat(void) {
	printf("Corriendo test_fase1_kat... ");

	/* 0xA5 = 10100101 */
	uint8_t buffer[] = { 0xA5 };
	bs_reader_t reader;
	bs_reader_init(&reader, buffer, 1);

	assert(bs_read_bits(&reader, 4) == 10); /* 1010 */
	assert(bs_read_bits(&reader, 4) == 5);  /* 0101 */

	printf("OK\n");
}

void test_init_and_state(void) {
	printf("Corriendo test_init_and_state... ");
	uint8_t buffer[] = { 0xFF, 0xFF };
	bs_reader_t bs;

	bs_reader_init(&bs, buffer, 2);

	assert(bs_reader_bits_left(&bs) == 16);
	assert(bs_reader_eof(&bs) == false);
	printf("OK\n");
}

void test_read_single_bits(void) {
	printf("Corriendo test_read_single_bits... ");
	uint8_t buffer[] = { 0xAC }; /* 1010 1100 */
	bs_reader_t bs;

	bs_reader_init(&bs, buffer, 1);

	assert(bs_read_bit(&bs) == 1);
	assert(bs_read_bit(&bs) == 0);
	assert(bs_read_bit(&bs) == 1);
	assert(bs_read_bit(&bs) == 0);

	assert(bs_read_bit(&bs) == 1);
	assert(bs_read_bit(&bs) == 1);
	assert(bs_read_bit(&bs) == 0);
	assert(bs_read_bit(&bs) == 0);

	assert(bs_reader_eof(&bs) == true);
	assert(bs_reader_bits_left(&bs) == 0);

	printf("OK\n");
}

void test_read_bits_cross_boundary(void) {
	printf("Corriendo test_read_bits_cross_boundary... ");
	uint8_t buffer[] = { 0xAC, 0x35 }; /* 10101100 00110101 */
	bs_reader_t bs;

	bs_reader_init(&bs, buffer, 2);

	assert(bs_read_bits(&bs, 4) == 10);
	assert(bs_read_bits(&bs, 6) == 48);
	assert(bs_read_bits(&bs, 6) == 53);
	assert(bs_reader_eof(&bs) == true);

	printf("OK\n");
}

void test_read_32_bits(void) {
	printf("Corriendo test_read_32_bits... ");
	uint8_t buffer[] = { 0x11, 0x22, 0x33, 0x44 };
	bs_reader_t bs;

	bs_reader_init(&bs, buffer, 4);

	uint32_t val = bs_read_bits(&bs, 32);
	assert(val == 0x11223344);
	(void)val;

	assert(bs_reader_eof(&bs) == true);

	printf("OK\n");
}

void test_bits_left_tracking(void) {
	printf("Corriendo test_bits_left_tracking... ");
	uint8_t buffer[] = { 0xAA, 0xBB, 0xCC };
	bs_reader_t bs;

	bs_reader_init(&bs, buffer, 3);

	assert(bs_reader_bits_left(&bs) == 24);

	bs_read_bits(&bs, 5);
	assert(bs_reader_bits_left(&bs) == 19);

	bs_read_bits(&bs, 15);
	assert(bs_reader_bits_left(&bs) == 4);

	bs_read_bits(&bs, 4);
	assert(bs_reader_bits_left(&bs) == 0);

	printf("OK\n");
}

void test_read_zero_bits(void) {
	printf("Corriendo test_read_zero_bits... ");
	uint8_t buffer[] = { 0xFF };
	bs_reader_t bs;
	bs_reader_init(&bs, buffer, 1);

	assert(bs_read_bits(&bs, 0) == 0);
	assert(bs_reader_bits_left(&bs) == 8);
	printf("OK\n");
}

/* ========================================================================
 * FASE 2: TESTS DEL ESCRITOR Y ESPEJO
 * ======================================================================== */

void test_writer_basic_byte(void) {
	printf("Corriendo test_writer_basic_byte... ");
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;

	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0xA, 4);
	bs_write_bits(&bw, 0x5, 4);

	assert(bs_writer_bytes_written(&bw) == 1);
	assert(buffer[0] == 0xA5);

	printf("OK\n");
}

void test_writer_flush_padding(void) {
	printf("Corriendo test_writer_flush_padding... ");
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0x5, 3);

	assert(bs_writer_bytes_written(&bw) == 0);

	bs_writer_flush(&bw);
	assert(bs_writer_bytes_written(&bw) == 1);
	assert(buffer[0] == 0xA0);

	printf("OK\n");
}

void test_writer_dirty_values(void) {
	printf("Corriendo test_writer_dirty_values... ");
	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	/* Valor sucio (0xFF) pero solo 2 bits: 11 */
	bs_write_bits(&bw, 0xFF, 2);
	/* Valor con ruido pero solo 6 bits: 000000 */
	bs_write_bits(&bw, 0xAA00, 6);

	/* 11 + 000000 = 11000000 (0xC0) */
	assert(buffer[0] == 0xC0);
	printf("OK\n");
}

void test_mirror_round_trip(void) {
	printf("Corriendo test_mirror_round_trip... ");

	uint32_t input_values[] = { 1, 5, 23, 0, 1023, 12, 1, 15, 2048, 3 };
	unsigned int input_lengths[] = { 1, 3, 5, 2, 10, 4, 1, 4, 12, 2 };
	int num_elements = (int)(sizeof(input_lengths) / sizeof(input_lengths[0]));

	/* Total = 1+3+5+2+10+4+1+4+12+2 = 44 bits. 6 bytes con padding. */
	uint8_t ram_buffer[16] = { 0 };

	bs_writer_t bw;
	bs_writer_init(&bw, ram_buffer, sizeof(ram_buffer));
	for (int i = 0; i < num_elements; i++) {
		bs_write_bits(&bw, input_values[i], input_lengths[i]);
	}
	bs_writer_flush(&bw);

	size_t total_bytes_output = bs_writer_bytes_written(&bw);
	assert(total_bytes_output == 6);

	bs_reader_t br;
	bs_reader_init(&br, ram_buffer, total_bytes_output);

	assert(bs_reader_bits_left(&br) == 48);

	for (int i = 0; i < num_elements; i++) {
		uint32_t read_val = bs_read_bits(&br, input_lengths[i]);
		if (read_val != input_values[i]) {
			printf("\nFALLO en indice %d: Esperado %u, Obtenido %u\n",
			       i, input_values[i], read_val);
			assert(false);
		}
	}

	assert(bs_reader_bits_left(&br) == 4);
	assert(bs_read_bits(&br, 4) == 0);
	assert(bs_reader_eof(&br) == true);

	printf("OK\n");
}

/* ========================================================================
 * BUG #1: Desbordamiento del bit_cache cuando n > 32 en bs_write_bits.
 *
 * FIX aplicado: assert(n <= 32) al inicio de bs_write_bits.
 * Esto convierte el desbordamiento silencioso en un fallo explicito en debug
 * y desaparece en release (-DNDEBUG).
 *
 * Los tests originales pasaban n=57/58, que violaba el contrato de la API
 * (value es uint32_t, n > 32 no tiene sentido). Los tests nuevos verifican:
 *   - Que el assert se dispara con n > 32 (via fork)
 *   - Que el contrato n <= 32 funciona correctamente en los limites
 *   - Que escrituras consecutivas de 32 bits (el caso real) son correctas
 * ======================================================================== */

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
	printf("Corriendo test_bug1_assert_fires_on_n_gt_32... ");

#if !HAS_FORK
	printf("SKIP (fork no disponible en esta plataforma)\n");
#elif defined(NDEBUG)
	printf("SKIP (asserts desactivados en release)\n");
#else
	int r = expect_abort(_bug1_write_n33);
	if (r == 1) { printf("OK (assert disparado correctamente)\n"); }
	else if (r == 0) { printf("FALLO: no se disparo el assert\n"); assert(false); }
	else { printf("SKIP (fork() fallo)\n"); }
#endif
}

/* n=32 es el limite maximo permitido: debe funcionar sin problemas.
 * Con 7 bits residuales en cache: 7+32=39 < 64, sin desbordamiento. */
void test_bug1_n32_is_safe_limit(void) {
	printf("Corriendo test_bug1_n32_is_safe_limit... ");

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

/* Escrituras consecutivas de 32 bits: el caso mas frecuente en la practica */
void test_bug1_consecutive_32bit_writes(void) {
	printf("Corriendo test_bug1_consecutive_32bit_writes... ");

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
			printf("\nFALLO en [%d]: esperado 0x%X, obtenido 0x%X\n", i, values[i], v);
			assert(false);
		}
	}
	printf("OK\n");
}

/* Patron alternante 1 bit + 32 bits: maximo estres para el mecanismo dump+residuo */
void test_bug1_alternating_1_and_32_bits(void) {
	printf("Corriendo test_bug1_alternating_1_and_32_bits... ");

	uint8_t buffer[64];
	memset(buffer, 0, sizeof(buffer));
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, sizeof(buffer));

	typedef struct { uint32_t val; unsigned int bits; } field_t;
	field_t fields[] = {
		{ 1,          1  },
		{ 0xAAAAAAAA, 32 },
		{ 0,          1  },
		{ 0x55555555, 32 },
		{ 1,          1  },
		{ 0xFFFFFFFF, 32 },
		{ 0,          1  },
		{ 0x00000000, 32 },
	};
	int count = (int)(sizeof(fields) / sizeof(fields[0]));

	for (int i = 0; i < count; i++) {
		bs_write_bits(&bw, fields[i].val, fields[i].bits);
	}
	bs_writer_flush(&bw);
	assert(bw.overflow == false);

	bs_reader_t br;
	bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
	for (int i = 0; i < count; i++) {
		uint32_t v = bs_read_bits(&br, fields[i].bits);
		if (v != fields[i].val) {
			printf("\nFALLO en campo %d: esperado 0x%X, obtenido 0x%X\n",
			       i, fields[i].val, v);
			assert(false);
		}
	}

	printf("OK\n");
}

/* ========================================================================
 * BUG #2: Undefined Behavior en bs_read_bits cuando n == 0 con cache lleno.
 *
 * FIX aplicado: `if (n == 0) return 0;` al inicio de bs_read_bits.
 * ======================================================================== */

void test_bug2_read_zero_bits_full_cache(void) {
	printf("Corriendo test_bug2_read_zero_bits_full_cache... ");

	/* 8 bytes: refill llena el cache a exactamente 64 bits */
	uint8_t buffer[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
	bs_reader_t bs;
	bs_reader_init(&bs, buffer, 8);

	/* Sin el fix, esto ejecuta (bit_cache >> 64): UB en C99 */
	uint32_t result = bs_read_bits(&bs, 0);

	assert(result == 0);
	(void)result;
	assert(bs_reader_bits_left(&bs) == 64);
	assert(bs_read_bits(&bs, 8) == 0x01);

	printf("OK\n");
}

void test_bug2_zero_bits_anywhere_in_stream(void) {
	printf("Corriendo test_bug2_zero_bits_anywhere_in_stream... ");

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

	printf("OK\n");
}

void test_bug2_write_zero_bits_is_noop(void) {
	printf("Corriendo test_bug2_write_zero_bits_is_noop... ");

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
	printf("OK\n");
}

/* ========================================================================
 * BUG #3: Overflow silencioso del buffer de salida.
 *
 * FIX aplicado: campo `bool overflow` en bs_writer_t, activado en _bs_writer_dump.
 * ======================================================================== */

void test_bug3_overflow_does_not_corrupt_written_bytes(void) {
	printf("Corriendo test_bug3_overflow_does_not_corrupt_written_bytes... ");

	uint8_t buffer[2] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 2);

	bs_write_bits(&bw, 0xAA, 8);
	bs_write_bits(&bw, 0xBB, 8);
	bs_write_bits(&bw, 0xCC, 8); /* no cabe */

	assert(bs_writer_bytes_written(&bw) == 2);
	assert(buffer[0] == 0xAA);
	assert(buffer[1] == 0xBB);
	assert(bw.overflow == true);

	printf("OK\n");
}

void test_bug3_overflow_mid_field_no_corruption(void) {
	printf("Corriendo test_bug3_overflow_mid_field_no_corruption... ");

	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0xAB, 8);
	bs_write_bits(&bw, 0xF,  4);
	bs_write_bits(&bw, 0xF,  4);
	bs_writer_flush(&bw);

	assert(buffer[0] == 0xAB);
	assert(bs_writer_bytes_written(&bw) == 1);
	assert(bw.overflow == true);
	printf("OK\n");
}

/* Overflow no se contagia: un writer nuevo empieza limpio */
void test_bug3_overflow_flag_resets_on_init(void) {
	printf("Corriendo test_bug3_overflow_flag_resets_on_init... ");

	uint8_t buffer[4] = { 0 };
	bs_writer_t bw;

	/* Primer uso: provocar overflow */
	bs_writer_init(&bw, buffer, 1);
	bs_write_bits(&bw, 0xAA, 8);
	bs_write_bits(&bw, 0xBB, 8);
	assert(bw.overflow == true);

	/* Re-init: overflow debe resetearse */
	bs_writer_init(&bw, buffer, 4);
	assert(bw.overflow == false);
	bs_write_bits(&bw, 0x11, 8);
	bs_write_bits(&bw, 0x22, 8);
	bs_writer_flush(&bw);
	assert(bw.overflow == false);
	assert(buffer[0] == 0x11);
	assert(buffer[1] == 0x22);

	printf("OK\n");
}

/* FIX #5: flush despues de overflow no produce underflow en padding_bits.
 * Si escribimos mucho mas alla del buffer, bits_in_cache crece (>= 8).
 * Sin el fix, flush calcularia padding_bits = 8 - bits_in_cache => underflow. */
void test_bug5_flush_after_sustained_overflow(void) {
	printf("Corriendo test_bug5_flush_after_sustained_overflow... ");

	uint8_t buffer[1] = { 0 };
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, 1);

	bs_write_bits(&bw, 0xAB, 8);  /* llena buffer */
	bs_write_bits(&bw, 0xCD, 8);  /* overflow: bits_in_cache = 8 */
	bs_write_bits(&bw, 0xEF, 8);  /* mas overflow: bits_in_cache = 16 */
	bs_write_bits(&bw, 0x12, 8);  /* bits_in_cache = 24 */

	/* Sin el fix del flush, padding_bits = 8 - 24 = underflow catastrofico
	 * que dispara el assert(n <= 32) con un valor enorme. */
	bs_writer_flush(&bw);

	/* El buffer original no debe corromperse */
	assert(buffer[0] == 0xAB);
	assert(bs_writer_bytes_written(&bw) == 1);
	assert(bw.overflow == true);

	printf("OK\n");
}

/* ========================================================================
 * BUG #4: bits_in_cache era `int`, podia producir size_t gigantesco.
 *
 * FIX aplicado: cambiado a `unsigned int`.
 * ======================================================================== */

void test_bug4_bits_left_never_wraps_around(void) {
	printf("Corriendo test_bug4_bits_left_never_wraps_around... ");

	uint8_t buffer[8];
	memset(buffer, 0xAA, sizeof(buffer));
	bs_reader_t bs;
	bs_reader_init(&bs, buffer, 8);

	assert(bs_reader_bits_left(&bs) == 64);

	bs_read_bits(&bs, 1);
	size_t left = bs_reader_bits_left(&bs);
	assert(left == 63);
	assert(left < 10000);

	bs_read_bits(&bs, 31);
	bs_read_bits(&bs, 31);
	assert(bs_reader_bits_left(&bs) == 1);

	bs_read_bits(&bs, 1);
	left = bs_reader_bits_left(&bs);
	assert(left == 0);
	assert(left < 10000);
	assert(bs_reader_eof(&bs) == true);
	(void)left;

	printf("OK\n");
}

/* ========================================================================
 * TESTS COMBINADOS: escenarios end-to-end
 * ======================================================================== */

void test_combined_huffman_like_stream(void) {
	printf("Corriendo test_combined_huffman_like_stream... ");

	typedef struct { uint32_t value; unsigned int bits; } field_t;
	field_t fields[] = {
		{ 1,          1  },
		{ 0,          1  },
		{ 7,          3  },
		{ 0xDEAD,     16 },
		{ 0xFFFFFFFF, 32 },
		{ 0,          1  },
		{ 31,         5  },
		{ 0x1FF,      9  },
		{ 3,          2  },
	};
	/* Total: 1+1+3+16+32+1+5+9+2 = 70 bits -> 9 bytes con 2 de padding */
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
	assert(bw.overflow == false);

	bs_reader_t br;
	bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
	for (int i = 0; i < count; i++) {
		uint32_t v = bs_read_bits(&br, fields[i].bits);
		if (v != fields[i].value) {
			printf("\nFALLO en campo %d (%d bits): esperado %u (0x%X), obtenido %u (0x%X)\n",
			       i, fields[i].bits, fields[i].value, fields[i].value, v, v);
			assert(false);
		}
	}
	assert(bs_read_bits(&br, 2) == 0);
	assert(bs_reader_eof(&br) == true);
	printf("OK\n");
}

void test_combined_max_uint32_aligned_and_not(void) {
	printf("Corriendo test_combined_max_uint32_aligned_and_not... ");

	uint8_t buffer[8];

	/* Alineado */
	memset(buffer, 0, sizeof(buffer));
	bs_writer_t bw;
	bs_writer_init(&bw, buffer, sizeof(buffer));
	bs_write_bits(&bw, 0xFFFFFFFF, 32);
	bs_writer_flush(&bw);

	bs_reader_t br;
	bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
	assert(bs_read_bits(&br, 32) == 0xFFFFFFFF);

	/* Desalineado */
	memset(buffer, 0, sizeof(buffer));
	bs_writer_init(&bw, buffer, sizeof(buffer));
	bs_write_bits(&bw, 1,          1);
	bs_write_bits(&bw, 0xFFFFFFFF, 32);
	bs_write_bits(&bw, 0,          1);
	bs_writer_flush(&bw);

	bs_reader_init(&br, buffer, bs_writer_bytes_written(&bw));
	assert(bs_read_bits(&br, 1)  == 1);
	assert(bs_read_bits(&br, 32) == 0xFFFFFFFF);
	assert(bs_read_bits(&br, 1)  == 0);
	printf("OK\n");
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

int main(void) {
	printf("=== FASE 1: LECTOR ===\n");
	test_init_and_state();
	test_fase1_kat();
	test_read_single_bits();
	test_read_bits_cross_boundary();
	test_read_32_bits();
	test_bits_left_tracking();
	test_read_zero_bits();
	printf("OK: Fase 1 completa.\n\n");

	printf("=== FASE 2: ESCRITOR Y ESPEJO ===\n");
	test_writer_basic_byte();
	test_writer_flush_padding();
	test_writer_dirty_values();
	test_mirror_round_trip();
	printf("OK: Fase 2 completa.\n\n");

	printf("=== BUG #1: assert(n <= 32) en bs_write_bits ===\n");
	test_bug1_assert_fires_on_n_gt_32();
	test_bug1_n32_is_safe_limit();
	test_bug1_consecutive_32bit_writes();
	test_bug1_alternating_1_and_32_bits();
	printf("\n");

	printf("=== BUG #2: early-return para n == 0 en bs_read_bits ===\n");
	test_bug2_read_zero_bits_full_cache();
	test_bug2_zero_bits_anywhere_in_stream();
	test_bug2_write_zero_bits_is_noop();
	printf("\n");

	printf("=== BUG #3: flag overflow en bs_writer_t ===\n");
	test_bug3_overflow_does_not_corrupt_written_bytes();
	test_bug3_overflow_mid_field_no_corruption();
	test_bug3_overflow_flag_resets_on_init();
	printf("\n");

	printf("=== BUG #5: flush protegido contra overflow sostenido ===\n");
	test_bug5_flush_after_sustained_overflow();
	printf("\n");

	printf("=== BUG #4: bits_in_cache como unsigned int ===\n");
	test_bug4_bits_left_never_wraps_around();
	printf("\n");

	printf("=== TESTS COMBINADOS ===\n");
	test_combined_huffman_like_stream();
	test_combined_max_uint32_aligned_and_not();
	printf("\n");

	printf("==========================================\n");
	printf("EXITO: Todos los tests pasaron.\n");
	return 0;
}
