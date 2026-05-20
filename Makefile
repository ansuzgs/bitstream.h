# ===========================================================================
# Makefile — bitstream.h
#
# Uso:
#   make            Compila en modo debug (asserts activos, sin optimizar)
#   make release    Compila en modo release (asserts desactivados, -O2)
#   make asan       Compila con AddressSanitizer (detecta out-of-bounds, UAF)
#   make ubsan      Compila con UndefinedBehaviorSanitizer (detecta UB)
#   make sanitize   Compila con ASAN + UBSAN combinados
#   make test       Compila en debug y ejecuta los tests
#   make test-all   Ejecuta los tests en debug, release y con sanitizers
#   make clean      Elimina todos los binarios generados
# ===========================================================================

CC       ?= gcc
CSTD     := -std=c99

# Warnings: nivel profesional, sin ser excesivamente pedante
WARNINGS := -Wall -Wextra -Wpedantic             \
            -Wshadow -Wconversion -Wsign-conversion \
            -Wdouble-promotion -Wformat=2           \
            -Wno-unused-function

# Flags por modo
DEBUG_FLAGS   := -g3 -O0 -DDEBUG
RELEASE_FLAGS := -O2 -DNDEBUG
ASAN_FLAGS    := -g3 -O1 -fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS   := -g3 -O1 -fsanitize=undefined -fno-omit-frame-pointer
SANIT_FLAGS   := -g3 -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

# Includes: raiz del proyecto (bitstream.h) + directorio de tests (test.h)
INCLUDES := -I. -Itests

# Fuentes de test (añadir nuevos test_*.c aqui)
TEST_DIR := tests
TEST_SRC := $(TEST_DIR)/run_all.c          \
            $(TEST_DIR)/test_reader.c      \
            $(TEST_DIR)/test_writer.c      \
            $(TEST_DIR)/test_regression.c  \
            $(TEST_DIR)/test_integration.c
HDR      := bitstream.h $(TEST_DIR)/test.h

BIN_DBG  := $(TEST_DIR)/test_debug
BIN_REL  := $(TEST_DIR)/test_release
BIN_ASAN := $(TEST_DIR)/test_asan
BIN_UB   := $(TEST_DIR)/test_ubsan
BIN_SAN  := $(TEST_DIR)/test_sanitize

ALL_BINS := $(BIN_DBG) $(BIN_REL) $(BIN_ASAN) $(BIN_UB) $(BIN_SAN)

# Ejemplos (añadir nuevos aqui)
EXAMPLES_DIR := examples
EXAMPLES_SRC := $(wildcard $(EXAMPLES_DIR)/*.c)
EXAMPLES_BIN := $(EXAMPLES_SRC:.c=)
ALL_BINS     += $(EXAMPLES_BIN)

# ---- Targets de compilacion ------------------------------------------------

.PHONY: all debug release asan ubsan sanitize test test-all examples clean help

all: debug

debug: $(BIN_DBG)
$(BIN_DBG): $(TEST_SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(INCLUDES) $(DEBUG_FLAGS) -o $@ $(TEST_SRC)

release: $(BIN_REL)
$(BIN_REL): $(TEST_SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(INCLUDES) $(RELEASE_FLAGS) -o $@ $(TEST_SRC)

asan: $(BIN_ASAN)
$(BIN_ASAN): $(TEST_SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(INCLUDES) $(ASAN_FLAGS) -o $@ $(TEST_SRC)

ubsan: $(BIN_UB)
$(BIN_UB): $(TEST_SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(INCLUDES) $(UBSAN_FLAGS) -o $@ $(TEST_SRC)

sanitize: $(BIN_SAN)
$(BIN_SAN): $(TEST_SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(INCLUDES) $(SANIT_FLAGS) -o $@ $(TEST_SRC)

# ---- Ejemplos ---------------------------------------------------------------
 
examples: $(EXAMPLES_BIN)
 
$(EXAMPLES_DIR)/%: $(EXAMPLES_DIR)/%.c bitstream.h
	$(CC) $(CSTD) $(WARNINGS) -I. $(DEBUG_FLAGS) -o $@ $<

# ---- Targets de ejecucion ---------------------------------------------------

test: debug
	@echo ""
	@echo "===== TEST: debug (asserts activos) ====="
	./$(BIN_DBG)

test-all: debug release sanitize
	@echo ""
	@echo "===== TEST: debug (asserts activos) ====="
	./$(BIN_DBG)
	@echo ""
	@echo "===== TEST: release (asserts desactivados, -O2) ====="
	./$(BIN_REL)
	@echo ""
	@echo "===== TEST: sanitizers (ASAN + UBSAN) ====="
	./$(BIN_SAN)
	@echo ""
	@echo "===== TODOS LOS MODOS PASARON ====="

# ---- Limpieza ---------------------------------------------------------------

clean:
	rm -f $(ALL_BINS)

# ---- Ayuda ------------------------------------------------------------------

help:
	@echo "Targets disponibles:"
	@echo "  make            Compila en debug"
	@echo "  make release    Compila en release (-O2, -DNDEBUG)"
	@echo "  make asan       Compila con AddressSanitizer"
	@echo "  make ubsan      Compila con UBSanitizer"
	@echo "  make sanitize   Compila con ASAN + UBSAN"
	@echo "  make test       Compila debug y ejecuta"
	@echo "  make test-all   Ejecuta en debug, release y sanitizer"
	@echo "  make examples   Compila los ejemplos en examples/"
	@echo "  make clean      Elimina binarios"
