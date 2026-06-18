CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
TARGET   := tlm_image

# ── Ruta de SystemC ────────────────────────────────────────────────────────────
# Se puede sobreescribir desde la terminal:  make SYSTEMC_HOME=/mi/ruta
SYSTEMC_HOME := /usr

SYSTEMC_HOME := /usr/local

INCLUDES := -I$(SYSTEMC_HOME)/include
LIBDIR := $(SYSTEMC_HOME)/lib-linux64

LIBS := -L$(LIBDIR) \
        -lsystemc \
        -lpthread

# ── Fuentes (agregar aquí cada nuevo módulo) ───────────────────────────────────
SRCS := src/sc_main.cpp \
        src/storage.cpp \
        src/accelerator.cpp

# Los .o van a la carpeta build/ para no ensuciar src/
OBJS := $(patsubst src/%.cpp, build/%.o, $(SRCS))

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all dirs gen run clean info

all: dirs $(TARGET)

dirs:
	@mkdir -p build images

## Compilar
$(TARGET): $(OBJS)
	@echo "\n[Link] $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "[OK] Binario listo: ./$(TARGET)\n"

build/%.o: src/%.cpp
	@echo "[CC]  $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

## Generar imagen de prueba con Python
gen:
	python3 scripts/gen_raw.py

## Todo junto: compilar, generar imagen y ejecutar
run: all gen
	./$(TARGET)

## Solo compilar y ejecutar (asumiendo que la imagen ya existe)
exec: all
	./$(TARGET)

## Limpiar objetos y binario
clean:
	rm -rf build $(TARGET)

## Mostrar configuración actual
info:
	@echo "SYSTEMC_HOME = $(SYSTEMC_HOME)"
	@echo "CXX          = $(CXX)"
	@echo "CXXFLAGS     = $(CXXFLAGS)"
	@ls -lh $(LIBDIR)/libsystemc* 2>/dev/null || \
		echo "  [!] libsystemc no encontrada"