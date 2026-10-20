"""Render icon.svg into the icons the builds use, in assets/icon/. Run by hand after changing
icon.svg (needs rsvg-convert on the PATH); the results are committed, since the console build
images can't render SVG.

    python3 tools/authoring/icons.py

    icon-16/32/48/64/256.png  PNGs (PC window icon, 3DS)
    workshop64.ico            Windows executable icon (16, 32, 48 and 256 px)
    ds.bmp                    DS banner icon: 32x32, 16 colours, colour 0 transparent
    wii.png                   Homebrew Channel icon: 128x48, the icon centred
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from texload import read_png  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
SVG = os.path.join(ROOT, 'icon.svg')
OUT = os.path.join(ROOT, 'assets', 'icon')


def render(size):
    """The icon at size x size, as rows of (r, g, b, a)."""
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, 'icon.png')
        subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size), SVG, '-o', png], check=True)
        (w, h), pixel = read_png(png)
        return [[pixel(x, y) for x in range(w)] for y in range(h)]


def write_png(path, rows):
    raw = b''.join(b'\0' + bytes(c for px in row for c in px) for row in rows)
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', len(rows[0]), len(rows), 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)
    return data


def write_ico(path, pngs):
    """An .ico holding PNG images: [(size, png bytes)]."""
    head = struct.pack('<HHH', 0, 1, len(pngs))
    offset = 6 + 16 * len(pngs)
    entries, body = b'', b''
    for size, data in pngs:
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset + len(body))
        body += data
    open(path, 'wb').write(head + entries + body)


def write_ds_bmp(path, rows):
    """A 4-bit BMP: transparent pixels are colour 0, the rest at most 15 colours (fewer bits per
    channel until they fit)."""
    for bits in range(8, 0, -1):
        def key(px):
            return tuple(c >> (8 - bits) << (8 - bits) for c in px[:3])
        colours = sorted({key(px) for row in rows for px in row if px[3] >= 128})
        if len(colours) <= 15:
            break
    palette = [(255, 0, 255)] + colours + [(0, 0, 0)] * (15 - len(colours))
    index = {c: i + 1 for i, c in enumerate(colours)}
    pixels = b''
    for row in reversed(rows):  # BMP rows run bottom to top; the high nibble is the left pixel
        idx = [index[key(px)] if px[3] >= 128 else 0 for px in row]
        pixels += bytes(idx[i] << 4 | idx[i + 1] for i in range(0, len(idx), 2))
    pal = b''.join(struct.pack('BBBB', b, g, r, 0) for r, g, b in palette)
    info = struct.pack('<IiiHHIIiiII', 40, len(rows[0]), len(rows), 1, 4, 0, len(pixels), 2835, 2835, 16, 16)
    head = struct.pack('<2sIHHI', b'BM', 14 + 40 + len(pal) + len(pixels), 0, 0, 14 + 40 + len(pal))
    open(path, 'wb').write(head + info + pal + pixels)


def main():
    os.makedirs(OUT, exist_ok=True)
    pngs = {}
    for size in (16, 32, 48, 64, 256):
        pngs[size] = write_png(os.path.join(OUT, f'icon-{size}.png'), render(size))
    write_ico(os.path.join(OUT, 'workshop64.ico'), [(s, pngs[s]) for s in (16, 32, 48, 256)])
    write_ds_bmp(os.path.join(OUT, 'ds.bmp'), render(32))
    icon = render(48)
    blank = (0, 0, 0, 0)
    write_png(os.path.join(OUT, 'wii.png'), [[blank] * 40 + row + [blank] * 40 for row in icon])
    print(f'wrote icons to {os.path.normpath(OUT)}')


main()
