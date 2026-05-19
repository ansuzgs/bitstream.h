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

SRC      := test.c
HDR      := bitstream.h

BIN_DBG  := test_debug
BIN_REL  := test_release
BIN_ASAN := test_asan
BIN_UB   := test_ubsan
BIN_SAN  := test_sanitize

ALL_BINS := $(BIN_DBG) $(BIN_REL) $(BIN_ASAN) $(BIN_UB) $(BIN_SAN)

# ---- Targets de compilacion ------------------------------------------------

.PHONY: all debug release asan ubsan sanitize test test-all clean help

all: debug

debug: $(BIN_DBG)
$(BIN_DBG): $(SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(DEBUG_FLAGS) -o $@ $(SRC)

release: $(BIN_REL)
$(BIN_REL): $(SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(RELEASE_FLAGS) -o $@ $(SRC)

asan: $(BIN_ASAN)
$(BIN_ASAN): $(SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(ASAN_FLAGS) -o $@ $(SRC)

ubsan: $(BIN_UB)
$(BIN_UB): $(SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(UBSAN_FLAGS) -o $@ $(SRC)

sanitize: $(BIN_SAN)
$(BIN_SAN): $(SRC) $(HDR)
	$(CC) $(CSTD) $(WARNINGS) $(SANIT_FLAGS) -o $@ $(SRC)

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
	@echo "  make clean      Elimina binarios"
