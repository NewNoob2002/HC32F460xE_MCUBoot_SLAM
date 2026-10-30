"""Regenerates the updater icon: hc32-updater.png (window) and hc32-updater.ico (Windows executable).

Requires Pillow. Run from any directory: python make_icon.py
"""

from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 256
SCALE = 4  # supersampling factor for smooth edges
TOP = (59, 130, 246)  # matches the UI accent family
BOTTOM = (29, 78, 216)
WHITE = (255, 255, 255, 255)


def render() -> Image.Image:
    big = SIZE * SCALE
    unit = big / 24  # draw on the same 24-unit grid as the UI stroke icons

    gradient = Image.new("RGBA", (big, big))
    pixels = gradient.load()
    for y in range(big):
        t = y / (big - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM)) + (255,)
        for x in range(big):
            pixels[x, y] = color
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(5.4 * unit), fill=255)
    icon = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    icon.paste(gradient, (0, 0), mask)

    draw = ImageDraw.Draw(icon)
    stroke = round(1.5 * unit)

    def u(value: float) -> int:
        return round(value * unit)

    # Chip body with pins on every side.
    draw.rounded_rectangle((u(6), u(6), u(18), u(18)), radius=u(2.2), outline=WHITE, width=stroke)
    for offset in (9.5, 14.5):
        draw.line((u(offset), u(3.4), u(offset), u(6)), fill=WHITE, width=stroke)
        draw.line((u(offset), u(18), u(offset), u(20.6)), fill=WHITE, width=stroke)
        draw.line((u(3.4), u(offset), u(6), u(offset)), fill=WHITE, width=stroke)
        draw.line((u(18), u(offset), u(20.6), u(offset)), fill=WHITE, width=stroke)

    # Upgrade arrow inside the chip.
    draw.line((u(12), u(15.2), u(12), u(9.2)), fill=WHITE, width=stroke)
    draw.polygon([(u(12), u(8.2)), (u(9.3), u(11.2)), (u(14.7), u(11.2))], fill=WHITE)
    return icon.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def main() -> None:
    here = Path(__file__).resolve().parent
    icon = render()
    icon.save(here / "hc32-updater.png")
    icon.save(here / "hc32-updater.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == "__main__":
    main()
