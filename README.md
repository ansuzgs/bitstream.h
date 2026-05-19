````markdown
# bitstream.h

`bitstream.h` es una librería de C de alto rendimiento y estilo **header-only** diseñada para la manipulación eficiente de flujos de bits arbitrarios.

---

## ¿Por qué `bitstream.h`?

El estándar de I/O de C trabaja en bloques de 8 bits (*byte-aligned*). Sin embargo, la mayoría de los formatos de datos modernos —como códecs de audio/video, protocolos de red, algoritmos de compresión como DEFLATE o codificación Huffman— almacenan información en longitudes no alineadas (por ejemplo: 3, 5 o 12 bits).

Esta librería proporciona los primitivos de bajo nivel necesarios para extraer y empaquetar estos datos, evitando el overhead de alineación y minimizando el acceso a memoria mediante una arquitectura de **bit-cache (acumulador) de 64 bits**.

---

# Roadmap de Desarrollo

El proyecto está estructurado en cuatro fases iterativas, asegurando que cada componente sea robusto y verificable antes de avanzar hacia optimizaciones complejas.

---

## Fase 1: El Lector (MSB-First)

### Objetivo

Construir el motor base de lectura. Implementar la extracción de bits desde un buffer de memoria, manejando el *refill* automático del caché.

### Funciones clave

```c
bs_reader_init(bs_reader_t *bs, const uint8_t *data, size_t size);
````

Inicializa el estado del lector (puntero, tamaño y caché).

```c
_bs_reader_refill(bs_reader_t *bs);
```

Función interna que carga bytes desde RAM al caché cuando este cae por debajo de 56 bits.

```c
bs_read_bits(bs_reader_t *bs, int n);
```

Extrae `n` bits del flujo.

```c
bs_read_bit(bs_reader_t *bs);
```

Alias optimizado para lectura de 1 bit.

```c
bs_reader_bits_left(const bs_reader_t *bs);
```

Calcula la cantidad de bits disponibles.

```c
bs_reader_eof(const bs_reader_t *bs);
```

Verifica el fin del flujo.

---

## Fase 2: El Escritor (MSB-First)

### Objetivo

Implementar la contraparte del lector: escritura eficiente en buffers, manejando el desbordamiento del caché hacia la RAM.

### Funciones clave

```c
bs_writer_init(bs_writer_t *bs, uint8_t *data, size_t size);
```

Inicializa el buffer destino.

```c
_bs_writer_dump(bs_writer_t *bs);
```

Vuelca el caché a RAM cuando existen 8 o más bits acumulados.

```c
bs_write_bits(bs_writer_t *bs, uint32_t value, int n);
```

Empaqueta `n` bits en el acumulador.

```c
bs_write_bit(bs_writer_t *bs, uint8_t value);
```

Alias optimizado para escritura de 1 bit.

```c
bs_writer_flush(bs_writer_t *bs);
```

Alinea a byte mediante relleno con ceros y realiza el volcado final.

```c
bs_writer_bytes_written(const bs_writer_t *bs);
```

Retorna el número real de bytes escritos.

---

## Fase 3: Versatilidad (Lookahead & LSB-First)

### Objetivo

Preparar la librería para códecs y protocolos reales que requieren inspeccionar el flujo sin consumir bits o utilizar orden LSB-first.

### Funciones clave

```c
bs_peek_bits(bs_reader_t *bs, int n);
```

Lee bits sin avanzar el puntero. Necesario para parsing de árboles Huffman y lookahead.

```c
bs_read_bits_lsb(bs_reader_t *bs, int n);
bs_write_bits_lsb(bs_writer_t *bs, ...);
```

Variantes para protocolos LSB-first.

```c
bs_writer_set_order(bs_writer_t *bs, bool lsb);
```

Selector de modo MSB/LSB.

---

## Fase 4: Robustness & Fast-Paths

### Objetivo

Optimización extrema para entornos de producción, eliminando validaciones redundantes en *hot loops*.

### Funciones clave

```c
bs_read_bits_fast(bs_reader_t *bs, int n);
```

Versión sin validación de `_refill()`. Asume que el buffer contiene suficientes datos.

```c
bs_write_bits_fast(bs_writer_t *bs, uint32_t value, int n);
```

Versión sin validación de overflow. Asume capacidad suficiente en el buffer.

```c
bs_align(bs_writer_t *bs);
```

Fuerza alineación al siguiente byte mediante relleno.

---

# Filosofía de Diseño

## Header-only

Integración directa en cualquier proyecto.

Simplemente copia el archivo `bitstream.h` y úsalo, sin dependencias externas ni compilación separada.

---

## Zero-cost Abstractions

Todas las funciones están declaradas como:

```c
static inline
```

Esto permite al compilador integrar el código directamente en el lugar de llamada, eliminando el costo de invocación de funciones.

---

## Lazy Evaluation

La memoria RAM solo se accede cuando:

* El acumulador de 64 bits está vacío (lector).
* El acumulador está lleno (escritor).

Esto minimiza accesos a memoria y mejora el rendimiento en workloads intensivos.

---

## C Estándar

Compatibilidad total con:

* C99
* `stdint.h`
* `stddef.h`

---

# Requisitos

* Compilador compatible con C99 o superior.
* Arquitectura con soporte para tipos de 64 bits (`uint64_t` requerido para el caché).

```
```
