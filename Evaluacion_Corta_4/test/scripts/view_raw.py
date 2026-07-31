#!/usr/bin/env python3
"""
view_raw.py  –  Visualizar archivos RAW (RGB o grayscale)

Uso:
    python3 scripts/view_raw.py                      # ver input.raw (RGB)
    python3 scripts/view_raw.py images/input.raw 3  # RGB explícito
    python3 scripts/view_raw.py images/out_gray.raw 1  # grayscale
"""
import numpy as np
import sys
import os

W, H = 1920, 1080

def view_raw(path: str, channels: int = None) -> None:
    if not os.path.exists(path):
        print(f"[ERROR] Archivo no encontrado: {path}")
        sys.exit(1)

    data = np.fromfile(path, dtype=np.uint8)

    # Detectar automáticamente según el tamaño del archivo si no se pasa el número de canales
    if channels is None:
        if data.size == W * H * 3:
            channels = 3
        elif data.size == W * H:
            channels = 1
        else:
            print(f" Tamaño no soportado: {data.size} bytes")
            channels = 3

    expected = W * H * channels
    if data.size != expected:
        print(f"[WARN] Tamaño inesperado: {data.size} bytes (esperado {expected})")

    if channels == 3:
        img = data[:W * H * 3].reshape(H, W, 3)
        cmap = None
        title = f"RGB  {W}x{H}  ({data.size:,} bytes)"
    else:
        img = data[:W * H].reshape(H, W)
        cmap = 'gray'
        title = f"Grayscale  {W}x{H}  ({data.size:,} bytes)"

    print(f"Archivo:  {path}")
    print(f"Forma:    {img.shape}")
    print(f"Min/Max:  {data.min()} / {data.max()}")
    print(f"Media:    {data.mean():.1f}")

    try:
        import matplotlib.pyplot as plt
        plt.figure(figsize=(12, 7))
        plt.imshow(img, cmap=cmap)
        plt.title(title)
        plt.axis('off')
        plt.tight_layout()

        out_png = path.replace('.raw', '_preview.png')
        plt.savefig(out_png, dpi=72)
        print(f"Preview:  {out_png}")
        plt.show()

    except ImportError:
        print("[INFO] Instalar matplotlib para ver imagen: pip3 install matplotlib")

if __name__ == "__main__":
    path     = sys.argv[1] if len(sys.argv) > 1 else "images/input.raw"
    channels = int(sys.argv[2]) if len(sys.argv) > 2 else None
    view_raw(path, channels)
