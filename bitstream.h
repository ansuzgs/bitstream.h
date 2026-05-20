#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
	const uint8_t *buffer; /* Puntero al buffer de memoria */
	size_t size; /* Tamaño total del buffer en bytes */
	size_t byte_pos; /* Posicion actual del byte */
	uint64_t bit_cache; /* Acumulador de bits */
	unsigned int bits_in_cache; /* Cuantos bits validos quedan en el acumulador */
} bs_reader_t;

typedef struct {
	uint8_t *buffer; /* puntero mutable */
	size_t size; /* Tamaño total de la memoria reservada para el buffer */
	size_t byte_pos; /* indice del proximo byte a escribir en la RAM */
	uint64_t bit_cache; /* acumulador donde inyectamos los bits temporalmente */
	unsigned int bits_in_cache; /* cuantos bits estan encolados y pendientes de volcarse */
	bool overflow; /* Flag de desbordamiento del buffer de salida */
} bs_writer_t;


static inline void bs_reader_init(bs_reader_t *bs, const uint8_t* data, size_t size);
static inline void bs_writer_init(bs_writer_t *bs, uint8_t *data, size_t size);

static inline uint8_t bs_read_bit(bs_reader_t *bs);
static inline uint32_t bs_read_bits(bs_reader_t *bs, unsigned int n);
static inline void bs_write_bit(bs_writer_t *bs, uint8_t value);
static inline void bs_write_bits(bs_writer_t *bs, uint32_t value, unsigned int n);

static inline uint32_t bs_peek_bits(bs_reader_t *bs, unsigned int n);
static inline void bs_skip_bits(bs_reader_t *bs, unsigned int n);

static inline uint32_t bs_read_bits_lsb(bs_reader_t *bs, unsigned int n);
static inline void bs_write_bits_lsb(bs_writer_t *bs, uint32_t value, unsigned int n);
static inline uint32_t bs_peek_bits_lsb(bs_reader_t *bs, unsigned int n);
static inline void bs_writer_flush_lsb(bs_writer_t *bs);

static inline bool bs_reader_eof(const bs_reader_t *bs);
static inline size_t bs_reader_bits_left(const bs_reader_t *bs);
static inline void bs_writer_flush(bs_writer_t *bs);
static inline size_t bs_writer_bytes_written(const bs_writer_t *bs);

static inline void _bs_reader_refill(bs_reader_t *bs);
static inline void _bs_reader_refill_lsb(bs_reader_t *bs);
static inline void _bs_writer_dump(bs_writer_t *bs);
static inline void _bs_writer_dump_lsb(bs_writer_t *bs);


static inline void bs_reader_init(bs_reader_t *bs, const uint8_t* data, size_t size) {
	bs->buffer = data;
	bs->size = size;
	bs->byte_pos = 0;
	bs->bit_cache = 0;
	bs->bits_in_cache = 0;
}

static inline void bs_writer_init(bs_writer_t *bs, uint8_t *data, size_t size) {
	bs->buffer = data;
	bs->size = size;
	bs->byte_pos = 0;
	bs->bit_cache = 0;
	bs->bits_in_cache = 0;
	bs->overflow = false;
}

static inline uint8_t bs_read_bit(bs_reader_t *bs) {
	return (uint8_t) bs_read_bits(bs, 1);
}

static inline uint32_t bs_read_bits(bs_reader_t *bs, unsigned int n) {
	if (n == 0) return 0;
	_bs_reader_refill(bs);

	if (bs->bits_in_cache < n) return 0;

	bs->bits_in_cache -= n;
	uint32_t result = (uint32_t)((bs->bit_cache >> bs->bits_in_cache) & ((1ULL << n) - 1));

	return result;
}

static inline void bs_write_bit(bs_writer_t *bs, uint8_t value) {
	bs->bit_cache = bs->bit_cache << 1 | (value & 1);
	bs->bits_in_cache++;

	if (bs->bits_in_cache >= 8) {
		_bs_writer_dump(bs);
	}
}

static inline void bs_write_bits(bs_writer_t *bs, uint32_t value, unsigned int n) {
	assert(n <= 32);
	if (n == 0) return;

	uint64_t mask = (1ULL << n) - 1;
	uint64_t clean_value = value & mask;

	bs->bit_cache = (bs->bit_cache << n) | clean_value;
	bs->bits_in_cache += n;

	if (bs->bits_in_cache >= 8) {
		_bs_writer_dump(bs);
	}
}

static inline uint32_t bs_peek_bits(bs_reader_t *bs, unsigned int n) {
	if (n == 0) return 0;
	_bs_reader_refill(bs);

	if (bs->bits_in_cache < n) return 0;

	uint32_t result = (uint32_t)((bs->bit_cache >> (bs->bits_in_cache - n)) & ((1ULL << n) - 1));

	return result;
}

static inline void bs_skip_bits(bs_reader_t *bs, unsigned int n) {
	if (n == 0) return;
	_bs_reader_refill(bs);

	if (bs->bits_in_cache < n) {
		bs->bits_in_cache = 0;
		return;
	}

	bs->bits_in_cache -= n;
}

static inline uint32_t bs_read_bits_lsb(bs_reader_t *bs, unsigned int n) {
	if (n == 0) return 0;
	_bs_reader_refill_lsb(bs);

	if (bs->bits_in_cache < n) return 0;

	bs->bits_in_cache -= n;

	uint32_t result = (uint32_t)(bs->bit_cache & ((1ULL << n) - 1));
	bs->bit_cache >>= n;

	return result;
}

static inline void bs_write_bits_lsb(bs_writer_t *bs, uint32_t value, unsigned int n) {
	assert(n <= 32);
	if (n == 0) return;

	uint64_t mask = (1ULL << n) - 1;
	uint64_t clean_value = value & mask;

	bs->bit_cache |= (clean_value << bs->bits_in_cache);

	bs->bits_in_cache += n;

	if (bs->bits_in_cache >= 8) {
		_bs_writer_dump_lsb(bs);
	}
}

static inline uint32_t bs_peek_bits_lsb(bs_reader_t *bs, unsigned int n) {
	if (n == 0) return 0;
	_bs_reader_refill_lsb(bs);

	if (bs->bits_in_cache < n) return 0;

	uint32_t result = (uint32_t)(bs->bit_cache & ((1ULL << n) - 1));
	return result;
}

static inline void bs_writer_flush_lsb(bs_writer_t *bs) {
	 if (bs->bits_in_cache == 0) return;

	 if (bs->overflow || bs->bits_in_cache >= 8) {
		 _bs_writer_dump_lsb(bs);
		 if (bs->bits_in_cache == 0) return;
		 if (bs->bits_in_cache >= 8) {
			 bs->overflow = true;
			 return;
		 }
	 }

	 unsigned int padding_bits = 8 - bs->bits_in_cache;
	 bs_write_bits_lsb(bs, 0, padding_bits);
}

static inline bool bs_reader_eof(const bs_reader_t *bs) {
	return (bs->byte_pos >= bs->size) && (bs->bits_in_cache == 0);
}

static inline size_t bs_reader_bits_left(const bs_reader_t *bs) {
	if (bs->byte_pos >= bs->size) {
		return (size_t)bs->bits_in_cache;
	}

	size_t bytes_left = bs->size - bs->byte_pos;
	return (bytes_left * 8) + bs->bits_in_cache;
}

static inline void bs_writer_flush(bs_writer_t *bs) {
	if (bs->bits_in_cache == 0) return;

	if (bs->overflow || bs->bits_in_cache >= 8) {
		/* El buffer desbordo y hay bits huerfanos en cache.
		 * Intentar un ultimo dump por si hay espacio residual. */
		_bs_writer_dump(bs);
		if (bs->bits_in_cache == 0) return;
		if (bs->bits_in_cache >= 8) {
			/* Aun no caben: no podemos alinear, solo marcar overflow */
			bs->overflow = true;
			return;
		}
	}

	unsigned int padding_bits = 8 - bs->bits_in_cache;
	bs_write_bits(bs, 0, padding_bits);
}

static inline size_t bs_writer_bytes_written(const bs_writer_t *bs) {
	return bs->byte_pos;
}

static inline void _bs_reader_refill(bs_reader_t *bs) {
	while (bs->bits_in_cache <= 56) {
		if (bs->byte_pos >= bs->size) {
			break;
		}

		uint8_t next_byte = bs->buffer[bs->byte_pos];

		bs->bit_cache = (bs->bit_cache << 8) | next_byte;

		bs->bits_in_cache += 8;
		bs->byte_pos++;
	}
}

static inline void _bs_reader_refill_lsb(bs_reader_t *bs) {
	while (bs->bits_in_cache <= 56) {
		if (bs->byte_pos >= bs->size) {
			break;
		}

		uint8_t next_byte = bs->buffer[bs->byte_pos];

		bs->bit_cache |= (uint64_t)next_byte << bs->bits_in_cache;

		bs->bits_in_cache += 8;
		bs->byte_pos++;
	}
}

static inline void _bs_writer_dump(bs_writer_t *bs) {
	while (bs->bits_in_cache >= 8) {
		if (bs->byte_pos >= bs->size) {
			bs->overflow = true;
			break;
		}
		unsigned int shift_amount = bs->bits_in_cache - 8;
		uint8_t byte_to_write = (uint8_t)((bs->bit_cache >> shift_amount) & 0xFF);

		bs->buffer[bs->byte_pos] = byte_to_write;
		bs->byte_pos++;

		bs->bits_in_cache -= 8;
	}

	bs->bit_cache &= ((1ULL << bs->bits_in_cache) - 1);
}

static inline void _bs_writer_dump_lsb(bs_writer_t *bs) {
	while (bs->bits_in_cache >= 8) {
		if (bs->byte_pos >= bs->size) {
			bs->overflow = true;
			break;
		}
		uint8_t byte_to_write = (uint8_t)(bs->bit_cache & 0xFF);

		bs->buffer[bs->byte_pos] = byte_to_write;
		bs->byte_pos++;

		bs->bit_cache >>= 8;
		bs->bits_in_cache -= 8;
	}
}

#endif // BITSTREAM_H
