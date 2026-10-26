#!/usr/bin/env python3
"""Internal QA for gui_shape's QMP screendump, using only the standard library.

This checks the chapter's five groups of blue circles, red square outlines and
green triangle outlines. It deliberately does not compare rasterizer boundary
pixels with Rust embedded-graphics, or add a student assessment requirement.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path

RGB = tuple[int, int, int]
BLUE: RGB = (0, 0, 255)
RED: RGB = (255, 0, 0)
GREEN: RGB = (0, 255, 0)
BLACK: RGB = (0, 0, 0)
WHITESPACE = b" \t\r\n\v\f"


def _token(data: bytes, position: int) -> tuple[bytes, int]:
    while position < len(data):
        if data[position] in WHITESPACE:
            position += 1
        elif data[position] == ord("#"):
            end = data.find(b"\n", position)
            if end < 0:
                raise ValueError("unterminated PPM header comment")
            position = end + 1
        else:
            break
    start = position
    while position < len(data) and data[position] not in WHITESPACE:
        position += 1
    if start == position:
        raise ValueError("incomplete PPM header")
    return data[start:position], position


def _read_ppm(path: str | Path) -> tuple[int, int, bytes]:
    data = Path(path).read_bytes()
    fields: list[bytes] = []
    position = 0
    for _ in range(4):
        field, position = _token(data, position)
        fields.append(field)
    if fields[0] != b"P6":
        raise ValueError("QMP screendump must be binary P6 PPM")
    try:
        width, height, maximum = map(int, fields[1:])
    except ValueError as error:
        raise ValueError("invalid PPM dimensions or maximum") from error
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError("PPM needs positive dimensions and 8-bit RGB samples")
    if position >= len(data) or data[position] not in WHITESPACE:
        raise ValueError("missing separator before PPM raster")
    # Consume the header separator only. Raster bytes may themselves be ASCII
    # whitespace; stripping all whitespace would corrupt a valid image.
    if data[position:position + 2] == b"\r\n":
        position += 2
    else:
        position += 1
    raster = data[position:]
    expected = width * height * 3
    if len(raster) != expected:
        raise ValueError(f"PPM raster has {len(raster)} bytes; expected {expected}")
    return width, height, raster


def check(path: str | Path) -> dict:
    """Return semantic image evidence, raising ValueError on a failed check."""
    width, height, raster = _read_ppm(path)
    if (width, height) != (1280, 800):
        raise ValueError(f"display is {width}x{height}; expected 1280x800")

    def pixel(x: int, y: int) -> RGB:
        offset = (y * width + x) * 3
        return tuple(raster[offset:offset + 3])

    def require(x: int, y: int, color: RGB, label: str) -> None:
        actual = pixel(x, y)
        if actual != color:
            raise ValueError(f"{label} at ({x},{y}) is RGB{actual}; expected RGB{color}")

    counts = Counter(
        tuple(raster[offset:offset + 3]) for offset in range(0, len(raster), 3)
    )
    # Wide lower bounds catch a few diagnostic colored dots being mistaken for
    # whole shapes, while allowing implementation-dependent outline boundaries.
    for color, name, minimum in ((BLUE, "blue", 70000),
                                 (RED, "red", 20000),
                                 (GREEN, "green", 10000)):
        if counts[color] < minimum:
            raise ValueError(f"only {counts[color]} {name} pixels; expected >= {minimum}")

    groups: list[dict] = []
    for number, center in enumerate((250, 420, 590, 760, 930), start=1):
        label = f"group {number}"
        # Sample safely inside the circle, away from any rasterized boundary.
        for dx, dy in ((0, 0), (-35, 0), (35, 0), (0, -35), (0, 35)):
            require(center + dx, 175 + dy, BLUE, label + " circle interior")
        # Four edge centers and a hollow center distinguish an outlined square.
        for dx, dy in ((-75, 0), (75, 0), (0, -75), (0, 75)):
            require(center + dx, 400 + dy, RED, label + " square outline")
        require(center, 400, BLACK, label + " square interior")
        # Three triangle edge midpoints; these are far from vertex rounding.
        for dx, y in ((40, 575), (-20, 650), (-60, 625)):
            require(center + dx, y, GREEN, label + " triangle outline")
        groups.append({"group": number, "center_x": center,
                       "circle": "blue", "square": "red outline",
                       "triangle": "green outline"})
    require(1200, 750, BLACK, "background")
    return {
        "width": width,
        "height": height,
        "groups": groups,
        "color_counts": {"blue": counts[BLUE], "red": counts[RED],
                         "green": counts[GREEN], "black": counts[BLACK]},
        "result": "PASS",
        "comparison": "semantic shapes and colors; no boundary pixel equality",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="P6 PPM from QMP screendump")
    arguments = parser.parse_args()
    try:
        evidence = check(arguments.image)
    except (OSError, ValueError) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print(json.dumps(evidence, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
