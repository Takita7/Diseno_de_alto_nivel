#!/usr/bin/env bash
# =============================================================================
# run_all.sh — Automatiza la construcción y ejecución completa del
# prototipo virtual (gem5 ARM64 + puente TLM 2.0 + Accelerator SystemC).
#
# Requisito previo: haber seguido SETUP_PROTOTIPO_VIRTUAL.md una vez
# (instalación de dependencias, gem5, SystemC, libm5.a, compilación inicial
# de util/tlm y de examples/accel_bridge). Este script automatiza el ciclo
# de "recompilar todo y correr", no la instalación inicial de herramientas.
#
# Uso:
#   GEM5_DIR=$HOME/gem5 REPO_DIR=$HOME/Diseno_de_alto_nivel/Evaluacion_Corta_2 \
#   SYSTEMC_HOME=/usr/local/systemc bash run_all.sh
# =============================================================================
set -euo pipefail

GEM5_DIR="${GEM5_DIR:-$HOME/gem5}"
REPO_DIR="${REPO_DIR:-$HOME/Diseno_de_alto_nivel/Evaluacion_Corta_2}"
SYSTEMC_HOME="${SYSTEMC_HOME:-/usr/local/systemc}"

GUEST_DIR="$REPO_DIR/guest"
TLM_DIR="$GEM5_DIR/util/tlm"
CONF_SCRIPT="$TLM_DIR/conf/tlm_soc.py"
GEM5_SC="$TLM_DIR/build/examples/accel_bridge/gem5.sc"

echo "== 1/5: Compilando programa bare-metal ARM64 (accel_test.elf) =="
make -C "$GUEST_DIR" clean
make -C "$GUEST_DIR"
test -f "$GUEST_DIR/accel_test.elf" || { echo "[ERROR] No se generó accel_test.elf"; exit 1; }

echo "== 2/5: Compilando el puente gem5<->SystemC propio (accel_bridge) =="
cd "$TLM_DIR"
scons "build/examples/accel_bridge/gem5.sc" -j2
test -x "$GEM5_SC" || { echo "[ERROR] No se generó $GEM5_SC"; exit 1; }

echo "== 3/5: Generando configuración de gem5 (config.ini) =="
cd "$GEM5_DIR"
"$GEM5_DIR/build/ARM/gem5.opt" "$CONF_SCRIPT" || true
# El "fatal: Can't find port handler type" en este paso es esperado:
# gem5.opt normal no entiende puertos 'tlm_*', solo genera el config.ini.
test -f "$GEM5_DIR/m5out/config.ini" || { echo "[ERROR] No se generó config.ini"; exit 1; }

echo "== 4/5: Ejecutando la co-simulación (gem5 + puente TLM + Accelerator) =="
cd "$TLM_DIR"
export LD_LIBRARY_PATH="$TLM_DIR/build/systemc:$SYSTEMC_HOME/lib:${LD_LIBRARY_PATH:-}"
echo "    (el programa termina en un bucle infinito intencional tras guardar"
echo "     output.raw — interrumpir con Ctrl+C al ver 'Procesamiento terminado')"
"./build/examples/accel_bridge/gem5.sc" "$GEM5_DIR/m5out/config.ini" || true

echo "== 5/5: Verificando resultado contra la referencia SystemC pura =="
REF_DIR="$REPO_DIR/test"
make -C "$REF_DIR" run SYSTEMC_HOME="$SYSTEMC_HOME" >/dev/null
GEM5_OUTPUT="$TLM_DIR/output.raw"
REF_OUTPUT="$REF_DIR/images/output.raw"

if [ -f "$GEM5_OUTPUT" ]; then
    if cmp -s "$GEM5_OUTPUT" "$REF_OUTPUT"; then
        echo "[OK] output.raw generado por gem5 es IDÉNTICO a la referencia SystemC."
    else
        echo "[!!] output.raw generado por gem5 DIFIERE de la referencia — revisar."
    fi
else
    echo "[!!] No se encontró $GEM5_OUTPUT — revisar que m5_write_file haya corrido."
fi
