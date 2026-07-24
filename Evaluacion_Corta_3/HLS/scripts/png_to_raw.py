#!/usr/bin/env python3
"""
gen_raw.py

Generate a synthetic 1920x1080 RGB RAW image for HLS testing.

Output format:
    R0 G0 B0 R1 G1 B1 ...

The generated image contains:
    - Red gradient from left to right
    - Green gradient from top to bottom
    - Constant blue value of 128

Generated file:
    hls/data/input.raw
"""

from pathlib import Path
import sys
import numpy as np

WIDTH = 1920
HEIGHT = 1080

SCRIPT_DIR = Path(__file__).resolve().parent
HLS_DIR = SCRIPT_DIR.parent

RAW_OUTPUT = HLS_DIR / "data" / "input.raw"


def generate_image() -> np.ndarray:
    """Generate the synthetic RGB test image."""

    image = np.zeros((HEIGHT, WIDTH, 3), dtype=np.uint8)

    # Red gradient (left -> right)
    red_gradient = np.linspace(0, 255, WIDTH, dtype=np.uint8)
    image[:, :, 0] = np.tile(red_gradient, (HEIGHT, 1))

    # Green gradient (top -> bottom)
    green_gradient = np.linspace(0, 255, HEIGHT, dtype=np.uint8)
    image[:, :, 1] = np.tile(green_gradient, (WIDTH, 1)).T

    # Constant blue channel
    image[:, :, 2] = 128

    return image


def save_raw(image: np.ndarray) -> None:
    """Save image as RGB888 RAW."""

    RAW_OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    image.tofile(RAW_OUTPUT)

    expected_size = WIDTH * HEIGHT * 3
    actual_size = RAW_OUTPUT.stat().st_size

    if actual_size != expected_size:
        raise RuntimeError(
            f"Unexpected RAW size: {actual_size} bytes "
            f"(expected {expected_size} bytes)"
        )


def main() -> int:

    try:
        print(f"Generating {WIDTH}x{HEIGHT} RGB RAW image...")

        image = generate_image()

        save_raw(image)

        print("Generation completed successfully.")
        print(f"Output file : {RAW_OUTPUT}")
        print(f"Resolution  : {WIDTH} x {HEIGHT}")
        print(f"Format      : RGB888")
        print(f"Size        : {RAW_OUTPUT.stat().st_size:,} bytes")

        return 0

    except Exception as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())