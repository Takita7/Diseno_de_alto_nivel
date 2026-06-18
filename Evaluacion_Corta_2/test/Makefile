# =============================================================================
# Makefile – TLM Image Processor (test stage)
# =============================================================================
# Mejoras:
#   - Dependency tracking automático (.d files)
#   - Variables organizadas por categoría
#   - Targets específicos (clean-build, rebuild, etc.)
#   - Mejor documentación interna
# =============================================================================

# ── CONFIGURACIÓN ──────────────────────────────────────────────────────────────
CXX           := g++
CXXFLAGS      := -std=c++17 -Wall -Wextra -g -DSC_ALLOW_DEPRECATED_IEEE_API
DEPFLAGS      := -MMD -MP                     # Genera .d files con dependencias
TARGET        := tlm_image

# ── RUTAS ──────────────────────────────────────────────────────────────────────
SRC_DIR       := src
BUILD_DIR     := build
IMG_DIR       := images

# SystemC (se puede sobreescribir: make SYSTEMC_HOME=/mi/ruta)
SYSTEMC_HOME  ?= /usr/local
INCLUDES      := -I$(SYSTEMC_HOME)/include
LIBDIR        := $(SYSTEMC_HOME)/lib-linux64
LIBS          := -L$(LIBDIR) -lsystemc -lpthread

# ── FUENTES Y OBJETOS ──────────────────────────────────────────────────────────
# Headers (.h files) — se usan solo para dependency tracking
HEADERS       := $(SRC_DIR)/storage.h \
                 $(SRC_DIR)/ram.h \
                 $(SRC_DIR)/bus.h \
                 $(SRC_DIR)/cpu.h \
                 $(SRC_DIR)/accelerator.h

# Fuentes compilables (.cpp files) — agregar aquí cada nuevo módulo
SRCS          := $(SRC_DIR)/sc_main.cpp \
                 $(SRC_DIR)/storage.cpp \
                 $(SRC_DIR)/accelerator.cpp

# Objetos y dependencias
OBJS          := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
DEPS          := $(OBJS:.o=.d)

# ── TARGETS PHONY ─────────────────────────────────────────────────────────────
.PHONY: all dirs gen run exec rebuild clean clean-build clean-all info help view compare view-show

# ── TARGETS REALES ────────────────────────────────────────────────────────────
all: dirs $(TARGET)
	@echo "\n✓ Build completado: ./$(TARGET)"

dirs:
	@mkdir -p $(BUILD_DIR) $(IMG_DIR)

$(TARGET): $(OBJS)
	@echo "\n[Link] $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "[OK] Binario listo: ./$(TARGET)\n"

# Compilación con dependency tracking automático
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | dirs
	@echo "[CC]  $<"
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c -o $@ $<

# Incluir .d files si existen (usado después de compilar)
-include $(DEPS)

# ── TARGETS ÚTILES ────────────────────────────────────────────────────────────
gen:
	@echo "[Gen] Generando imagen de prueba..."
	python3 scripts/gen_raw.py

run: all gen
	@echo "[Run] Ejecutando simulación..."
	./$(TARGET)
	@echo "\n[Vis] Generando figura de comparación..."
	python3 scripts/compare_raw.py --save

exec: all
	@echo "[Run] Ejecutando simulación (imagen existente)..."
	./$(TARGET)

view:
	@echo "[Vis] Visualizando salida en escala de grises..."
	python3 scripts/view_raw.py $(IMG_DIR)/output.raw 1

compare:
	@echo "[Cmp] Generando comparación entrada vs salida..."
	python3 scripts/compare_raw.py 

view-show: compare
	@echo "[Vis] Abriendo figura de comparación..."
	xdg-open $(IMG_DIR)/comparison.png 2>/dev/null || open $(IMG_DIR)/comparison.png 2>/dev/null || echo "  [!] No se pudo abrir imagen automáticamente"

rebuild: clean all
	@echo "✓ Rebuild completado"

clean-build:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "✓ Objetos y binario eliminados"

clean-all: clean-build
	@rm -rf $(IMG_DIR)/*.raw
	@echo "✓ Imágenes generadas también eliminadas"

clean: clean-build

# ── INFORMACIÓN ────────────────────────────────────────────────────────────────
info:
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║ TLM Image Processor – Configuración                           ║"
	@echo "╠════════════════════════════════════════════════════════════════╣"
	@echo "║ SYSTEMC_HOME    = $(SYSTEMC_HOME)"
	@echo "║ CXX             = $(CXX)"
	@echo "║ CXXFLAGS        = $(CXXFLAGS)"
	@echo "║ Target          = $(TARGET)"
	@echo "║ Archivos fuente = $(words $(SRCS)) ($(SRCS))"
	@echo "║ Headers         = $(words $(HEADERS)) ($(HEADERS))"
	@echo "╠════════════════════════════════════════════════════════════════╣"
	@ls -lh $(LIBDIR)/libsystemc* 2>/dev/null || echo "  [!] libsystemc no encontrada en $(LIBDIR)"
	@echo "╚════════════════════════════════════════════════════════════════╝"

help:
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║ Targets disponibles                                           ║"
	@echo "╠════════════════════════════════════════════════════════════════╣"
	@echo "║ make all         – Compilar (default)                         ║"
	@echo "║ make gen         – Generar imagen de prueba                   ║"
	@echo "║ make run         – Compilar, simular y generar comparación    ║"
	@echo "║ make exec        – Ejecutar (imagen existente)                ║"
	@echo "║ make view        – Ver salida en escala de grises             ║"
	@echo "║ make compare     – Generar figura: input.raw vs output.raw    ║"
	@echo "║ make view-show   – Mostrar figura de comparación              ║"
	@echo "║ make rebuild     – Limpiar y compilar todo                    ║"
	@echo "║ make clean       – Eliminar objetos y binario                 ║"
	@echo "║ make clean-build – Alias para clean                          ║"
	@echo "║ make clean-all   – Eliminar todo (incluyendo imágenes)        ║"
	@echo "║ make info        – Mostrar configuración                      ║"
	@echo "║ make help        – Este mensaje                               ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"