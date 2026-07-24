#!/usr/bin/env python3
#!/usr/bin/env python3
"""
raw_to_png.py

Convert 1920x1080 RAW images into PNG images for visualization.

Supported RAW formats:

    RGB888
        1920 x 1080 x 3 bytes
        Byte order:
            R0 G0 B0 R1 G1 B1 ...

    8-bit Grayscale
        1920 x 1080 x 1 byte
        Byte order:
            Gray0 Gray1 Gray2 ...

Examples

    python scripts/raw_to_png.py

    python scripts/raw_to_png.py data/input.raw

    python scripts/raw_to_png.py data/output_hls_csim.raw

    python scripts/raw_to_png.py data/output_hls_cosim.raw

The generated PNG is automatically stored inside:

    HLS/images/

using the same filename as the RAW file.
"""

from pathlib import Path
import sys

import numpy as np


WIDTH = 1920
HEIGHT = 1080

RGB_CHANNELS = 3
GRAY_CHANNELS = 1

RGB_SIZE = WIDTH * HEIGHT * RGB_CHANNELS
GRAY_SIZE = WIDTH * HEIGHT

SCRIPT_DIR = Path(__file__).resolve().parent
HLS_DIR = SCRIPT_DIR.parent
DATA_DIR = HLS_DIR / "data"
IMAGES_DIR = HLS_DIR / "images"

DEFAULT_INPUT = DATA_DIR / "input.raw"


def detect_channels(byte_count: int) -> int:
    """Detect whether a RAW file is RGB or grayscale."""

    if byte_count == RGB_SIZE:
        return RGB_CHANNELS

    if byte_count == GRAY_SIZE:
        return GRAY_CHANNELS

    raise ValueError(
        f"Unsupported RAW size: {byte_count} bytes. "
        f"Expected {RGB_SIZE} bytes for RGB888 or "
        f"{GRAY_SIZE} bytes for grayscale."
    )


def create_output_path(input_path: Path) -> Path:
    """Create the PNG output path inside the hls/images directory."""

    IMAGES_DIR.mkdir(parents=True, exist_ok=True)

    return IMAGES_DIR / f"{input_path.stem}.png"


def load_raw(input_path: Path, channels: int | None) -> tuple[np.ndarray, int]:
    """Read and reshape a RAW image."""

    if not input_path.exists():
        raise FileNotFoundError(
            f"RAW file not found: {input_path}"
        )

    data = np.fromfile(input_path, dtype=np.uint8)

    if channels is None:
        channels = detect_channels(data.size)

    if channels not in (GRAY_CHANNELS, RGB_CHANNELS):
        raise ValueError(
            f"Invalid channel count: {channels}. "
            "Only 1-channel grayscale and 3-channel RGB are supported."
        )

    expected_size = WIDTH * HEIGHT * channels

    if data.size != expected_size:
        raise ValueError(
            f"Invalid RAW size for {channels} channel(s): "
            f"{data.size} bytes received, "
            f"{expected_size} bytes expected."
        )

    if channels == RGB_CHANNELS:
        image = data.reshape((HEIGHT, WIDTH, RGB_CHANNELS))
    else:
        image = data.reshape((HEIGHT, WIDTH))

    return image, channels


def save_png(
    image: np.ndarray,
    output_path: Path,
    channels: int
) -> None:
    """Save the RAW image as a PNG using matplotlib."""

    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise RuntimeError(
            "matplotlib is required to generate the PNG. "
            "Install it with: python -m pip install matplotlib"
        ) from error

    figure = plt.figure(figsize=(12, 7))

    if channels == RGB_CHANNELS:
        plt.imshow(image)
        image_type = "RGB888"
    else:
        plt.imshow(
            image,
            cmap="gray",
            vmin=0,
            vmax=255
        )
        image_type = "8-bit grayscale"

    plt.title(
        f"{image_type} — {WIDTH}x{HEIGHT}"
    )
    plt.axis("off")
    plt.tight_layout()

    figure.savefig(
        output_path,
        dpi=100,
        bbox_inches="tight",
        pad_inches=0
    )

    plt.close(figure)


def print_information(
    input_path: Path,
    output_path: Path,
    image: np.ndarray,
    channels: int
) -> None:
    """Print information about the converted image."""

    image_type = (
        "RGB888"
        if channels == RGB_CHANNELS
        else "8-bit grayscale"
    )

    print("========================================")
    print(" RAW to PNG conversion completed")
    print("========================================")
    print(f"RAW input:   {input_path}")
    print(f"PNG output:  {output_path}")
    print(f"Resolution:  {WIDTH} x {HEIGHT}")
    print(f"Format:      {image_type}")
    print(f"Shape:       {image.shape}")
    print(f"Minimum:     {int(image.min())}")
    print(f"Maximum:     {int(image.max())}")
    print(f"Mean:        {float(image.mean()):.2f}")


def main() -> int:
    try:
        input_path = (
            Path(sys.argv[1]).resolve()
            if len(sys.argv) >= 2
            else DEFAULT_INPUT
        )

        channels = (
            int(sys.argv[2])
            if len(sys.argv) >= 3
            else None
        )

        image, detected_channels = load_raw(
            input_path,
            channels
        )

        output_path = create_output_path(input_path)

        save_png(
            image,
            output_path,
            detected_channels
        )

        print_information(
            input_path,
            output_path,
            image,
            detected_channels
        )

        return 0

    except Exception as error:
        print(
            f"[ERROR] {error}",
            file=sys.stderr
        )
        return 1


if __name__ == "__main__":
    sys.exit(main())