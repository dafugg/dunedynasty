#!/usr/bin/env python3
"""Convert a 24/32 bits-per-pixel ZPixmap XWD screen dump (from xwd -root) to PNG."""
import struct
import sys

from PIL import Image


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    hdr = struct.unpack('>25I', data[:100])
    header_size, width, height = hdr[0], hdr[4], hdr[5]
    byte_order, bpp, bytes_per_line = hdr[7], hdr[11], hdr[12]
    ncolors = hdr[19]
    pixels = data[header_size + ncolors * 12:]
    if bpp == 32:
        raw = 'BGRX' if byte_order == 0 else 'XRGB'
    elif bpp == 24:
        raw = 'BGR' if byte_order == 0 else 'RGB'
    else:
        sys.exit(f'unsupported bits_per_pixel {bpp}')
    img = Image.frombuffer('RGB', (width, height), pixels, 'raw', raw, bytes_per_line, 1)
    img.save(dst)


if __name__ == '__main__':
    main()
