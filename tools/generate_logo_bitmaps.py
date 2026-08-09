from pathlib import Path
from PIL import Image


SOURCE = Path(__file__).parents[1] / "assets" / "bitcoin_logo_orange.png"
SIZES = ((16, 24), (24, 36), (34, 50), (44, 64))


def make_bitmap(source: Image.Image, width: int, height: int) -> list[int]:
    alpha = source.getchannel("A")
    crop = alpha.crop(alpha.getbbox())
    crop.thumbnail((width - 2, height - 2), Image.Resampling.LANCZOS)
    canvas = Image.new("L", (width, height), 0)
    canvas.paste(crop, ((width - crop.width) // 2, (height - crop.height) // 2))
    values: list[int] = []
    for y in range(height):
        for byte_x in range((width + 7) // 8):
            value = 0
            for bit in range(8):
                x = byte_x * 8 + bit
                if x < width and canvas.getpixel((x, y)) >= 96:
                    value |= 1 << bit
            values.append(value)
    return values


def main() -> None:
    source = Image.open(SOURCE).convert("RGBA")
    print("#ifndef BITCOIN_LOGO_BITMAPS_H")
    print("#define BITCOIN_LOGO_BITMAPS_H")
    print("\n#include <Arduino.h>\n")
    for width, height in SIZES:
        values = make_bitmap(source, width, height)
        name = f"BTC_LOGO_{width}X{height}"
        print(f"static const uint8_t {name}[] PROGMEM = {{")
        for start in range(0, len(values), 12):
            row = ", ".join(f"0x{value:02X}" for value in values[start:start + 12])
            print(f"  {row},")
        print("};\n")
    print("#endif")


if __name__ == "__main__":
    main()
