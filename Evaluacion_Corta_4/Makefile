# =============================================================================
# Makefile – TLM Image Processor  
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
VERILATOR      := verilator
VL_DIR         := $(BUILD_DIR)/verilator
VL_TARGET      := $(VL_DIR)/Vram_axi4
VL_LIB_DIR     := $(BUILD_DIR)/verilator_lib
VL_LIB         := $(VL_LIB_DIR)/Vram_axi4__ALL.a
VL_HEADER      := $(VL_LIB_DIR)/Vram_axi4.h
RTL_TOP        := ram_axi4
VERILATOR_ROOT ?= $(shell verilator --getenv VERILATOR_ROOT 2>/dev/null || echo /usr/share/verilator)

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
        view compare view-show verilate verilate_lib run_vl run_vl_trace \
        clean_vl lint all_phase5 run_dpi clean_dpi golden verify_dpi

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
# lint     - solo verifica sintaxis y semantica del RTL
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

# Genera el modelo como libreria estatica (sin sim_main, para linkear con SystemC)
verilate_lib: | $(VL_LIB_DIR)
	@echo "[VL]  Generando libreria C++ del RTL..."
	$(VERILATOR) --cc --build \
		-CFLAGS "-std=c++17" \
		--Mdir $(VL_LIB_DIR) \
		$(RTL_DIR)/$(RTL_TOP).sv
	@echo "[OK]  Libreria lista:"
	@echo "       Header : $(VL_HEADER)"
	@echo "       Lib    : $(VL_LIB)"
	@echo "       Include: $(VERILATOR_ROOT)/include"

run_vl: verilate
	@echo "[Run] Corriendo testbench Verilator..."
	$(VL_TARGET)

run_vl_trace: verilate
	@echo "[Run] Corriendo con VCD trace..."
	$(VL_TARGET) +trace
	@echo "[OK]  Waveforms en sim_main.vcd"

$(VL_DIR):
	@mkdir -p $(VL_DIR)

$(VL_LIB_DIR):
	@mkdir -p $(VL_LIB_DIR)

clean_vl:
	@rm -rf $(VL_DIR) $(VL_LIB_DIR)
	@echo "[OK]  Artefactos Verilator eliminados"

# ── PHASE 5: SystemC + RTL Verilog (Verilator co-simulacion) ─────────────────
DPI_TARGET   := tlm_image_dpi
DPI_BUILD    := $(BUILD_DIR)/dpi

# Includes: src/ + SystemC + Vram_axi4.h + verilated.h
DPI_INCLUDES := -I$(SRC_DIR) \
                -I$(SYSTEMC_HOME)/include \
                -I$(VL_LIB_DIR) \
                -I$(VERILATOR_ROOT)/include

# Fuentes C++ de Phase 5
DPI_SC_SRCS := $(SRC_DIR)/storage.cpp \
               $(SRC_DIR)/accelerator.cpp
DPI_TOP_SRC := $(TEST_DIR)/top_test_DPI.cpp
DPI_VL_RT   := $(VERILATOR_ROOT)/include/verilated.cpp \
               $(VERILATOR_ROOT)/include/verilated_threads.cpp

DPI_OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(DPI_BUILD)/%.o, $(DPI_SC_SRCS)) \
            $(DPI_BUILD)/top_test_DPI.o \
            $(DPI_BUILD)/verilated.o \
            $(DPI_BUILD)/verilated_threads.o

# all_phase5: genera libreria Verilator y compila el binario DPI
all_phase5: verilate_lib $(DPI_TARGET)
	@echo "\n[OK] Phase 5 listo: ./$(DPI_TARGET)"

$(DPI_TARGET): $(DPI_OBJS) $(VL_LIB)
	@echo "\n[Link-DPI] $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "[OK] Binario Phase 5: ./$(DPI_TARGET)\n"

# Compilacion fuentes SystemC (storage, accelerator)
$(DPI_BUILD)/%.o: $(SRC_DIR)/%.cpp | $(DPI_BUILD)
	@echo "[CC-DPI] $<"
	$(CXX) $(CXXFLAGS) $(DPI_INCLUDES) -c -o $@ $<

# Compilacion top_test_DPI.cpp
$(DPI_BUILD)/top_test_DPI.o: $(TEST_DIR)/top_test_DPI.cpp | $(DPI_BUILD)
	@echo "[CC-DPI] $<"
	$(CXX) $(CXXFLAGS) $(DPI_INCLUDES) -c -o $@ $<

# Runtime de Verilator (siempre necesario para linkear)
# -w suprime warnings de headers internos de Verilator (no son nuestros)
$(DPI_BUILD)/verilated.o: $(VERILATOR_ROOT)/include/verilated.cpp | $(DPI_BUILD)
	@echo "[CC-VL]  verilated.cpp"
	$(CXX) $(CXXFLAGS) -w $(DPI_INCLUDES) -c -o $@ $<

$(DPI_BUILD)/verilated_threads.o: $(VERILATOR_ROOT)/include/verilated_threads.cpp | $(DPI_BUILD)
	@echo "[CC-VL]  verilated_threads.cpp"
	$(CXX) $(CXXFLAGS) -w $(DPI_INCLUDES) -c -o $@ $<

$(DPI_BUILD):
	@mkdir -p $(DPI_BUILD)

# Correr Phase 5 (genera imagen si no existe)
run_dpi: all_phase5
	@echo "[Run] Phase 5 — SystemC + RTL Verilog..."
	@if [ ! -f $(IMG_DIR)/input.raw ]; then \
		echo "[Gen] Generando imagen de prueba..."; \
		cd $(TEST_DIR) && python3 scripts/gen_raw.py; \
	fi
	cd $(TEST_DIR) && ../$(DPI_TARGET)
	@echo "\n[Vis] Generando comparacion..."
	cd $(TEST_DIR) && python3 scripts/compare_raw.py --save

clean_dpi:
	@rm -rf $(DPI_BUILD) $(DPI_TARGET)
	@echo "[OK] Artefactos Phase 5 eliminados"

# ── VERIFICACION FORMAL ────────────────────────────────────────────────────────
# golden     - corre Phase 1 y guarda output como referencia
# verify_dpi - corre Phase 5 y compara contra el golden byte a byte

golden: all gen
	@echo "[Run] Generando golden desde Phase 1 (TLM directo)..."
	cd $(TEST_DIR) && ../$(TARGET)
	cp $(IMG_DIR)/output.raw $(IMG_DIR)/output_golden.raw
	@echo "[OK]  Golden guardado: $(IMG_DIR)/output_golden.raw"
	@echo "      Tamanio: $$(wc -c < $(IMG_DIR)/output_golden.raw) bytes"

verify_dpi: run_dpi
	@echo "\n[Verify] Comparando Phase 5 vs Golden Phase 1..."
	@if [ ! -f $(IMG_DIR)/output_golden.raw ]; then \
		echo "[ERROR] No existe output_golden.raw"; \
		echo "        Correr 'make golden' primero."; \
		exit 1; \
	fi
	cd $(TEST_DIR) && python3 scripts/verify_golden.py


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
	@echo "║         TLM Image Processor — Configuración                    ║"
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
	@echo "║  --- SystemC Phase 1/2 ---                                     ║"
	@echo "║ make all            Compilar SystemC                           ║"
	@echo "║ make run            Compilar + imagen + simular                ║"
	@echo "║ make exec           Simular (imagen ya existente)              ║"
	@echo "║ make compare        Figura: input.raw vs output.raw            ║"
	@echo "║  --- Verilator standalone ---                                  ║"
	@echo "║ make lint           Lint del RTL                               ║"
	@echo "║ make verilate       Modelo C++ + compilar sim_main             ║"
	@echo "║ make run_vl         Testbench C++ standalone                   ║"
	@echo "║ make run_vl_trace   Igual con VCD trace                        ║"
	@echo "║  --- Phase 5: SystemC + RTL Verilog ---                        ║"
	@echo "║ make verilate_lib   Generar libreria Verilator                 ║"
	@echo "║ make all_phase5     Compilar binario DPI                       ║"
	@echo "║ make run_dpi        Correr sistema completo Phase 5            ║"
	@echo "║ make clean_dpi      Limpiar artefactos Phase 5                 ║"
	@echo "║  --- General ---                                               ║"
	@echo "║ make clean          Eliminar objetos y binario                 ║"
	@echo "║ make clean-all      Eliminar todo (incluyendo imagenes)        ║"
	@echo "║ make clean_vl       Eliminar artefactos Verilator              ║"
	@echo "║ make info           Configuracion del build                    ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"