#!/usr/bin/env python3
"""
gen_raw.py  –  Genera imagen de prueba 1920x1080 en formato RAW RGB
               (binario plano, sin cabecera, orden: R G B R G B ...)

El degradado de colores es útil para verificar visualmente la conversión
a escala de grises: el canal R aumenta de izquierda a derecha,
el canal G aumenta de arriba a abajo, y B es constante.
"""
import numpy as np
import os
import sys

W, H  = 1920, 1080
OUT   = "images/input.raw"

os.makedirs("images", exist_ok=True)

print(f"Generando imagen {W}x{H} RGB...")

img = np.zeros((H, W, 3), dtype=np.uint8)

# R: degradado horizontal (negro en la izquierda, rojo en la derecha)
img[:, :, 0] = np.tile(np.linspace(0, 255, W, dtype=np.uint8), (H, 1))

# G: degradado vertical (negro arriba, verde abajo)
img[:, :, 1] = np.tile(np.linspace(0, 255, H, dtype=np.uint8), (W, 1)).T

# B: constante 128 (azul medio en toda la imagen)
img[:, :, 2] = 128

# Guardar como binario plano (formato RAW: R0 G0 B0 R1 G1 B1 ...)
img.tofile(OUT)

total = W * H * 3
print(f"  Guardado: {OUT}")
print(f"  Tamaño:   {total:,} bytes ({total / 1024 / 1024:.2f} MB)")
print(f"  Pixel [0,0]      R={img[0,0,0]:3d} G={img[0,0,1]:3d} B={img[0,0,2]:3d}")
print(f"  Pixel [0,1919]   R={img[0,-1,0]:3d} G={img[0,-1,1]:3d} B={img[0,-1,2]:3d}")
print(f"  Pixel [1079,0]   R={img[-1,0,0]:3d} G={img[-1,0,1]:3d} B={img[-1,0,2]:3d}")
print(f"  Pixel [1079,1919]R={img[-1,-1,0]:3d} G={img[-1,-1,1]:3d} B={img[-1,-1,2]:3d}")

# Guardar preview PNG si matplotlib está disponible
try:
    import matplotlib.pyplot as plt
    plt.figure(figsize=(12, 6))
    plt.subplot(1, 1, 1)
    plt.imshow(img)
    plt.title(f"input.raw  ({W}x{H} RGB)")
    plt.axis('off')
    plt.tight_layout()
    plt.savefig("images/input_preview.png", dpi=72)
    print(f"  Preview: images/input_preview.png")
except ImportError:
    print("  (matplotlib no disponible: sin preview PNG)")
