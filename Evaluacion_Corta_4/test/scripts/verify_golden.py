#!/usr/bin/env python3
# =============================================================================
# verify_golden.py  -  Verificacion formal: Phase 5 vs Golden Phase 1
#
# Compara byte a byte el output de Phase 5 (RTL Verilog) contra el
# golden de referencia generado por Phase 1 (TLM directo).
#
# Uso:
#   python3 scripts/verify_golden.py
#   python3 scripts/verify_golden.py --golden images/output_golden.raw \
#                                    --output images/output.raw
#
# Exit code: 0 = identico, 1 = diferente o error
# =============================================================================

import sys
import os
import argparse
import struct

# ── Colores ANSI ──────────────────────────────────────────────────────────────
GRN  = "\033[32m"
RED  = "\033[31m"
CYN  = "\033[36m"
YEL  = "\033[33m"
RST  = "\033[0m"

# ── Constantes de imagen ──────────────────────────────────────────────────────
WIDTH      = 1920
HEIGHT     = 1080
GRAY_SIZE  = WIDTH * HEIGHT   # 2,073,600 bytes

def load_raw(path: str) -> bytes:
    if not os.path.exists(path):
        print(f"{RED}[ERROR]{RST} Archivo no encontrado: {path}")
        sys.exit(1)
    with open(path, "rb") as f:
        data = f.read()
    print(f"  Cargado: {path}  ({len(data):,} bytes)")
    return data

def verify(golden: bytes, output: bytes) -> bool:
    print(f"\n{CYN}=== Verificacion formal: Phase 5 vs Golden ==={RST}\n")

    # ── 1. Tamanio ────────────────────────────────────────────────────────────
    print(f"[1] Tamanio:")
    print(f"    Golden  : {len(golden):,} bytes")
    print(f"    Output  : {len(output):,} bytes")
    print(f"    Esperado: {GRAY_SIZE:,} bytes")

    if len(golden) != GRAY_SIZE:
        print(f"  {RED}[FAIL]{RST} Golden tiene tamanio incorrecto")
        return False
    if len(output) != GRAY_SIZE:
        print(f"  {RED}[FAIL]{RST} Output tiene tamanio incorrecto")
        return False
    print(f"  {GRN}[OK]{RST} Tamanios correctos")

    # ── 2. Comparacion byte a byte ────────────────────────────────────────────
    print(f"\n[2] Comparacion byte a byte ({GRAY_SIZE:,} bytes)...")

    mismatches = []
    for i, (g, o) in enumerate(zip(golden, output)):
        if g != o:
            mismatches.append((i, g, o))
            if len(mismatches) >= 10:   # mostrar max 10 diferencias
                break

    if not mismatches:
        print(f"  {GRN}[OK]{RST} IDENTICO — todos los bytes coinciden")
    else:
        print(f"  {RED}[FAIL]{RST} {len(mismatches)} diferencia(s) encontradas "
              f"(mostrando primeras {len(mismatches)}):")
        for idx, (row, col) in [
            (i, divmod(i, WIDTH)) for i, _, _ in mismatches
        ]:
            g_val = golden[idx]
            o_val = output[idx]
            print(f"    byte[{idx:8d}] pixel({row:4d},{col:4d}): "
                  f"golden=0x{g_val:02X} output=0x{o_val:02X} "
                  f"diff={abs(int(g_val)-int(o_val))}")
        return False

    # ── 3. Estadisticas de luminancia ─────────────────────────────────────────
    print(f"\n[3] Estadisticas de luminancia:")

    import statistics
    g_mean = sum(golden) / len(golden)
    o_mean = sum(output) / len(output)
    g_min, g_max = min(golden), max(golden)
    o_min, o_max = min(output), max(output)

    print(f"    {'':12s}  {'Golden':>10s}  {'Output':>10s}")
    print(f"    {'Min':12s}  {g_min:>10d}  {o_min:>10d}")
    print(f"    {'Max':12s}  {g_max:>10d}  {o_max:>10d}")
    print(f"    {'Media':12s}  {g_mean:>10.3f}  {o_mean:>10.3f}")

    if abs(g_mean - o_mean) > 0.001:
        print(f"  {YEL}[WARN]{RST} Medias difieren en {abs(g_mean-o_mean):.4f}")
    else:
        print(f"  {GRN}[OK]{RST} Estadisticas identicas")

    # ── 4. Verificacion de primeros/ultimos bytes ─────────────────────────────
    print(f"\n[4] Muestras representativas:")
    print(f"    Primeros 8 bytes — golden: {list(golden[:8])}")
    print(f"    Primeros 8 bytes — output: {list(output[:8])}")
    print(f"    Ultimos  8 bytes — golden: {list(golden[-8:])}")
    print(f"    Ultimos  8 bytes — output: {list(output[-8:])}")

    return True

def main():
    parser = argparse.ArgumentParser(
        description="Verificacion formal: compara output Phase 5 vs golden Phase 1")
    parser.add_argument("--golden",
                        default="images/output_golden.raw",
                        help="Archivo golden (Phase 1)")
    parser.add_argument("--output",
                        default="images/output.raw",
                        help="Archivo a verificar (Phase 5)")
    args = parser.parse_args()

    print(f"{CYN}Cargando archivos...{RST}")
    golden = load_raw(args.golden)
    output = load_raw(args.output)

    ok = verify(golden, output)

    print()
    if ok:
        print(f"{GRN}╔══════════════════════════════════════════════╗{RST}")
        print(f"{GRN}║  VERIFICACION FORMAL: PASS                   ║{RST}")
        print(f"{GRN}║  Phase 5 (RTL) == Phase 1 (golden)           ║{RST}")
        print(f"{GRN}║  La integracion Verilog es correcta          ║{RST}")
        print(f"{GRN}╚══════════════════════════════════════════════╝{RST}")
        sys.exit(0)
    else:
        print(f"{RED}╔══════════════════════════════════════════════╗{RST}")
        print(f"{RED}║  VERIFICACION FORMAL: FAIL                   ║{RST}")
        print(f"{RED}║  Phase 5 difiere del golden de Phase 1       ║{RST}")
        print(f"{RED}╚══════════════════════════════════════════════╝{RST}")
        sys.exit(1)

if __name__ == "__main__":
    main()