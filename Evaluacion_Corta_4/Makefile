# =============================================================================
# Makefile – TLM Image Processor  (ec4/)
#
# Estructura esperada:
#   ec4/
#   ├── src/        accelerator.cpp/h  bus.h  cpu.h  ram.h  storage.cpp/h
#   ├── test/       top_test.cpp  images/  scripts/
#   ├── rtl/        (Fase 3 – Verilog)
#   ├── uvm/        (Fase 4 – UVM)
#   └── Makefile    ← este archivo, en la raíz
# =============================================================================

# ── HERRAMIENTAS ───────────────────────────────────────────────────────────────
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -DSC_ALLOW_DEPRECATED_IEEE_API
DEPFLAGS := -MMD -MP
TARGET   := tlm_image

# ── DIRECTORIOS ────────────────────────────────────────────────────────────────
SRC_DIR   := src
TEST_DIR  := test
BUILD_DIR := build
IMG_DIR   := $(TEST_DIR)/images
SCRIPTS   := $(TEST_DIR)/scripts

# ── SystemC (sobreescribir si es necesario: make SYSTEMC_HOME=/mi/ruta) ───────
SYSTEMC_HOME ?= /usr/local
INCLUDES     := -I$(SYSTEMC_HOME)/include -I$(SRC_DIR)
LIBDIR       := $(SYSTEMC_HOME)/lib-linux64
LIBS         := -L$(LIBDIR) -lsystemc -lpthread

# ── FUENTES ────────────────────────────────────────────────────────────────────
# Módulos SystemC en src/
SRC_SRCS  := $(SRC_DIR)/storage.cpp \
             $(SRC_DIR)/accelerator.cpp

# Top-level / testbench en test/
TEST_SRCS := $(TEST_DIR)/top_test.cpp

ALL_SRCS  := $(SRC_SRCS) $(TEST_SRCS)

# ── OBJETOS Y DEPENDENCIAS ─────────────────────────────────────────────────────
# src/  → build/src/<name>.o
# test/ → build/test/<name>.o   (subdirectorio separado = sin conflicto de patrones)
SRC_OBJS  := $(patsubst $(SRC_DIR)/%.cpp,  $(BUILD_DIR)/src/%.o,  $(SRC_SRCS))
TEST_OBJS := $(patsubst $(TEST_DIR)/%.cpp, $(BUILD_DIR)/test/%.o, $(TEST_SRCS))
OBJS      := $(SRC_OBJS) $(TEST_OBJS)
DEPS      := $(OBJS:.o=.d)

# ── PHONY ──────────────────────────────────────────────────────────────────────
.PHONY: all dirs gen run exec rebuild clean clean-build clean-all info help \
        view compare view-show

# ── TARGETS PRINCIPALES ────────────────────────────────────────────────────────
all: dirs $(TARGET)
	@echo "\n✓ Build completado: ./$(TARGET)"

dirs:
	@mkdir -p $(BUILD_DIR)/src $(BUILD_DIR)/test $(IMG_DIR)

$(TARGET): $(OBJS)
	@echo "\n[Link] $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "[OK] Binario listo: ./$(TARGET)\n"

# Regla para fuentes en src/ → build/src/
$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.cpp | dirs
	@echo "[CC]  $<"
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

# Regla para fuentes en test/ → build/test/
$(BUILD_DIR)/test/%.o: $(TEST_DIR)/%.cpp | dirs
	@echo "[CC]  $<"
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

-include $(DEPS)

# ── FLUJO COMPLETO ─────────────────────────────────────────────────────────────
# NOTA: todos los comandos que invocan el binario o scripts Python hacen
# "cd $(TEST_DIR)" primero, para que los paths relativos del C++
# ("images/input.raw", etc.) resuelvan a test/images/ y no a la raíz.

gen:
	@echo "[Gen] Generando imagen de prueba..."
	cd $(TEST_DIR) && python3 scripts/gen_raw.py

run: all gen
	@echo "[Run] Ejecutando simulación..."
	cd $(TEST_DIR) && ../$(TARGET)
	@echo "\n[Vis] Generando figura de comparación..."
	cd $(TEST_DIR) && python3 scripts/compare_raw.py --save

exec: all
	@echo "[Run] Ejecutando simulación (imagen existente)..."
	cd $(TEST_DIR) && ../$(TARGET)

view:
	@echo "[Vis] Visualizando salida en escala de grises..."
	cd $(TEST_DIR) && python3 scripts/view_raw.py images/output.raw 1

compare:
	@echo "[Cmp] Generando comparación entrada vs salida..."
	cd $(TEST_DIR) && python3 scripts/compare_raw.py

view-show: compare
	@echo "[Vis] Abriendo figura de comparación..."
	xdg-open $(IMG_DIR)/comparison.png 2>/dev/null || \
	open     $(IMG_DIR)/comparison.png 2>/dev/null || \
	echo "  [!] No se pudo abrir imagen automáticamente"

# ── LIMPIEZA ───────────────────────────────────────────────────────────────────
rebuild: clean all
	@echo "✓ Rebuild completado"

clean-build:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "✓ build/ y binario eliminados"

clean-all: clean-build
	@rm -f $(IMG_DIR)/*.raw $(IMG_DIR)/*.png
	@echo "✓ Imágenes generadas también eliminadas"

clean: clean-build

# ── INFORMACIÓN ────────────────────────────────────────────────────────────────
info:
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║         TLM Image Processor — Configuración                   ║"
	@echo "╠════════════════════════════════════════════════════════════════╣"
	@echo "║ SYSTEMC_HOME = $(SYSTEMC_HOME)"
	@echo "║ CXX          = $(CXX)"
	@echo "║ CXXFLAGS     = $(CXXFLAGS)"
	@echo "║ Target       = $(TARGET)"
	@echo "║ src/         = $(SRC_SRCS)"
	@echo "║ test/        = $(TEST_SRCS)"
	@echo "╠════════════════════════════════════════════════════════════════╣"
	@ls -lh $(LIBDIR)/libsystemc* 2>/dev/null || \
	 echo "  [!] libsystemc no encontrada en $(LIBDIR)"
	@echo "╚════════════════════════════════════════════════════════════════╝"

help:
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║              Targets disponibles                               ║"
	@echo "╠════════════════════════════════════════════════════════════════╣"
	@echo "║ make all          Compilar (default)                           ║"
	@echo "║ make gen          Generar imagen de prueba RAW                 ║"
	@echo "║ make run          Compilar + generar imagen + simular          ║"
	@echo "║ make exec         Simular (imagen ya existente)                ║"
	@echo "║ make view         Ver salida en escala de grises               ║"
	@echo "║ make compare      Generar figura: input.raw vs output.raw      ║"
	@echo "║ make view-show    Abrir figura de comparación                  ║"
	@echo "║ make rebuild      Limpiar y compilar todo                      ║"
	@echo "║ make clean        Eliminar objetos y binario                   ║"
	@echo "║ make clean-all    Eliminar todo (incluyendo imágenes)          ║"
	@echo "║ make info         Mostrar configuración del build              ║"
	@echo "║ make help         Este mensaje                                 ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"