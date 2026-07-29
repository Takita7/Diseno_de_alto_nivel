# =============================================================================
# Makefile – TLM Image Processor  (ec4/)
#
# Estructura esperada:
#   ec4/
#   ├── src/        accelerator.cpp/h  bus.h  cpu.h  ram.h  storage.cpp/h
#   ├── test/       top_test.cpp  images/  scripts/  sim_main.cpp
#   ├── rtl/        ram_axi4.sv
#   ├── uvm/        UVM testbench
#   └── Makefile    ← este archivo, en la raiz
# =============================================================================

# ── HERRAMIENTAS ───────────────────────────────────────────────────────────────
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -DSC_ALLOW_DEPRECATED_IEEE_API
DEPFLAGS := -MMD -MP
TARGET   := tlm_image

# ── DIRECTORIOS ────────────────────────────────────────────────────────────────
SRC_DIR   := src
TEST_DIR  := test
RTL_DIR   := rtl
BUILD_DIR := build
IMG_DIR   := $(TEST_DIR)/images
SCRIPTS   := $(TEST_DIR)/scripts

# ── SystemC (sobreescribir si es necesario: make SYSTEMC_HOME=/mi/ruta) ───────
SYSTEMC_HOME ?= /usr/local
INCLUDES     := -I$(SYSTEMC_HOME)/include -I$(SRC_DIR)
LIBDIR       := $(SYSTEMC_HOME)/lib-linux64
LIBS         := -L$(LIBDIR) -lsystemc -lpthread

# ── Verilator ─────────────────────────────────────────────────────────────────
VERILATOR     := verilator
VL_DIR        := $(BUILD_DIR)/verilator
VL_TARGET     := $(VL_DIR)/Vram_axi4
RTL_TOP       := ram_axi4

# ── FUENTES SystemC ────────────────────────────────────────────────────────────
SRC_SRCS  := $(SRC_DIR)/storage.cpp \
             $(SRC_DIR)/accelerator.cpp
TEST_SRCS := $(TEST_DIR)/top_test.cpp
ALL_SRCS  := $(SRC_SRCS) $(TEST_SRCS)

# ── OBJETOS Y DEPENDENCIAS ─────────────────────────────────────────────────────
SRC_OBJS  := $(patsubst $(SRC_DIR)/%.cpp,  $(BUILD_DIR)/src/%.o,  $(SRC_SRCS))
TEST_OBJS := $(patsubst $(TEST_DIR)/%.cpp, $(BUILD_DIR)/test/%.o, $(TEST_SRCS))
OBJS      := $(SRC_OBJS) $(TEST_OBJS)
DEPS      := $(OBJS:.o=.d)

# ── PHONY ──────────────────────────────────────────────────────────────────────
.PHONY: all dirs gen run exec rebuild clean clean-build clean-all info help \
        view compare view-show verilate run_vl clean_vl lint

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

# ── VERILATOR ─────────────────────────────────────────────────────────────────
# lint   - solo verifica sintaxis y semantica del RTL
# verilate - genera el modelo C++ y compila junto con sim_main.cpp
# run_vl   - corre el testbench C++ standalone
# clean_vl - elimina solo los artefactos de Verilator

lint:
	@echo "[Lint] Verificando rtl/$(RTL_TOP).sv..."
	$(VERILATOR) --lint-only -Wall $(RTL_DIR)/$(RTL_TOP).sv
	@echo "[OK]  Lint limpio"

verilate: | $(VL_DIR)
	@echo "[VL]  Generando modelo C++ desde $(RTL_DIR)/$(RTL_TOP).sv..."
	$(VERILATOR) --cc --exe --build --trace \
		-CFLAGS "-std=c++17 -Wall" \
		--Mdir $(VL_DIR) \
		$(RTL_DIR)/$(RTL_TOP).sv \
		$(abspath $(TEST_DIR)/sim_main.cpp)
	@echo "[OK]  Verilator build completo: $(VL_TARGET)"

run_vl: verilate
	@echo "[Run] Corriendo testbench Verilator..."
	$(VL_TARGET)

run_vl_trace: verilate
	@echo "[Run] Corriendo con VCD trace..."
	$(VL_TARGET) +trace
	@echo "[OK]  Waveforms en sim_main.vcd"

$(VL_DIR):
	@mkdir -p $(VL_DIR)

clean_vl:
	@rm -rf $(VL_DIR)
	@echo "[OK]  Artefactos Verilator eliminados"

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
	@echo "║  --- SystemC (Phase 1/2) ---                                   ║"
	@echo "║ make all          Compilar SystemC                             ║"
	@echo "║ make gen          Generar imagen de prueba RAW                 ║"
	@echo "║ make run          Compilar + generar imagen + simular          ║"
	@echo "║ make exec         Simular (imagen ya existente)                ║"
	@echo "║ make compare      Generar figura: input.raw vs output.raw      ║"
	@echo "║  --- Verilator (Phase 5) ---                                   ║"
	@echo "║ make lint         Lint del RTL con Verilator                   ║"
	@echo "║ make verilate     Generar modelo C++ + compilar sim_main       ║"
	@echo "║ make run_vl       Correr testbench C++ standalone              ║"
	@echo "║ make run_vl_trace Igual pero con VCD trace                     ║"
	@echo "║ make clean_vl     Eliminar artefactos Verilator                ║"
	@echo "║  --- General ---                                               ║"
	@echo "║ make rebuild      Limpiar y compilar todo                      ║"
	@echo "║ make clean        Eliminar objetos y binario                   ║"
	@echo "║ make clean-all    Eliminar todo (incluyendo imagenes)          ║"
	@echo "║ make info         Mostrar configuracion del build              ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"