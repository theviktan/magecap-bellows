"""Builds an app icon's images from its SVG source.

For an icon <Folder>/<Name>.svg, writes <Name>.png (512 px: window icon and logos) and <Name>.ico
(16 to 256 px: the Windows program icon) next to it. Both are checked in, so building needs neither tool.
With no argument it builds the MageCap Bellows icon, Resources/Icons/Bellows.svg.

Needs rsvg-convert (librsvg; on Debian/Ubuntu `apt install librsvg2-bin`) and Pillow.

    python Tools/GenerateAppIcon.py
    python Tools/GenerateAppIcon.py ForgeScript/Icons/ForgeScript.svg
"""
import argparse
import io
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = REPOSITORY / "Resources" / "Icons" / "Bellows.svg"
# Each size is drawn from the SVG at that size (sharper than shrinking the master image).
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
MASTER_SIZE = 512


def render(source: pathlib.Path, size: int) -> Image.Image:
    result = subprocess.run(["rsvg-convert", "--width", str(size), "--height", str(size), str(source)],
                            capture_output=True, check=True)
    return Image.open(io.BytesIO(result.stdout)).convert("RGBA")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build an app icon's PNG and ICO from its SVG")
    parser.add_argument("source", nargs="?", type=pathlib.Path, default=DEFAULT_SOURCE,
                        help="The icon's SVG (default: Resources/Icons/Bellows.svg)")
    args = parser.parse_args()
    source = args.source.resolve()
    if source.suffix.lower() != ".svg" or not source.is_file():
        print(f"{args.source} is not an SVG file", file=sys.stderr)
        return 1
    if shutil.which("rsvg-convert") is None:
        print("rsvg-convert was not found; install librsvg (e.g. apt install librsvg2-bin)", file=sys.stderr)
        return 1

    png = source.with_suffix(".png")
    ico = source.with_suffix(".ico")
    render(source, MASTER_SIZE).save(png, optimize=True)
    images = {size: render(source, size) for size in ICO_SIZES}
    largest = images[ICO_SIZES[-1]]
    largest.save(ico, sizes=[(size, size) for size in ICO_SIZES],
                 append_images=[images[size] for size in ICO_SIZES[:-1]])
    print(f"Wrote {png} and {ico}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
