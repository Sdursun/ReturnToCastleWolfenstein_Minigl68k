#!/usr/bin/env python3
"""
Makes rtcw-sp.info, the Workbench icon of the game: a 64x64 GlowIcon with
32 colours (OS 3.5 and newer), and the same picture in 4 colours as the
classic image for older Workbenches.

The picture is drawn here (a castle tower at night), so the icon can be
made again and changed: python3 mkicon.py [output.info] [preview.png]
Needs Pillow.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 64
SCALE = 4                       # drawn at 256x256, then scaled down
COLOURS = 32                    # with the transparent colour 0


def draw():
    S = SIZE * SCALE
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def r(x0, y0, x1, y1, fill):
        d.rectangle([x0 * SCALE, y0 * SCALE, x1 * SCALE - 1, y1 * SCALE - 1], fill=fill)

    # moon behind the tower
    d.ellipse([34 * SCALE, 4 * SCALE, 58 * SCALE, 28 * SCALE], fill=(236, 228, 190, 255))
    d.ellipse([39 * SCALE, 9 * SCALE, 44 * SCALE, 14 * SCALE], fill=(214, 204, 166, 255))
    d.ellipse([47 * SCALE, 17 * SCALE, 51 * SCALE, 21 * SCALE], fill=(214, 204, 166, 255))

    # rocky ground
    d.polygon([(2 * SCALE, 62 * SCALE), (10 * SCALE, 53 * SCALE), (22 * SCALE, 51 * SCALE),
               (44 * SCALE, 51 * SCALE), (55 * SCALE, 54 * SCALE), (62 * SCALE, 62 * SCALE)],
              fill=(58, 52, 46, 255))
    d.polygon([(6 * SCALE, 62 * SCALE), (14 * SCALE, 56 * SCALE), (50 * SCALE, 56 * SCALE),
               (58 * SCALE, 62 * SCALE)], fill=(40, 36, 32, 255))

    # tower body: lit left side, darker right side
    r(17, 20, 32, 56, (132, 128, 122, 255))
    r(32, 20, 45, 56, (92, 90, 86, 255))
    # battlements
    r(14, 14, 48, 21, (112, 108, 104, 255))
    for x in (14, 23, 32, 41):
        r(x, 9, x + 7, 15, (124, 120, 114, 255) if x < 32 else (96, 94, 90, 255))
    # shadow under the battlements
    r(17, 21, 45, 23, (60, 58, 56, 255))

    # stone courses
    for y in range(26, 56, 5):
        d.line([17 * SCALE, y * SCALE, 45 * SCALE - 1, y * SCALE], fill=(70, 68, 64, 255), width=SCALE // 2)
        off = 0 if (y // 5) % 2 else 4
        for x in range(17 + off, 45, 8):
            d.line([x * SCALE, y * SCALE, x * SCALE, (y + 5) * SCALE], fill=(76, 74, 70, 255), width=SCALE // 2)

    # arched window with warm light
    d.rectangle([27 * SCALE, 31 * SCALE, 35 * SCALE - 1, 42 * SCALE - 1], fill=(255, 168, 48, 255))
    d.ellipse([27 * SCALE, 27 * SCALE, 35 * SCALE - 1, 35 * SCALE - 1], fill=(255, 168, 48, 255))
    d.rectangle([28 * SCALE, 33 * SCALE, 34 * SCALE - 1, 41 * SCALE - 1], fill=(255, 214, 110, 255))
    d.line([31 * SCALE, 28 * SCALE, 31 * SCALE, 42 * SCALE], fill=(60, 40, 24, 255), width=SCALE)
    d.line([27 * SCALE, 36 * SCALE, 35 * SCALE, 36 * SCALE], fill=(60, 40, 24, 255), width=SCALE)

    # door
    d.rectangle([28 * SCALE, 48 * SCALE, 34 * SCALE - 1, 56 * SCALE - 1], fill=(48, 30, 20, 255))
    d.ellipse([28 * SCALE, 45 * SCALE, 34 * SCALE - 1, 51 * SCALE - 1], fill=(48, 30, 20, 255))

    # flag pole and a plain dark red flag
    d.line([20 * SCALE, 0, 20 * SCALE, 10 * SCALE], fill=(40, 36, 34, 255), width=SCALE)
    d.polygon([(21 * SCALE, 1 * SCALE), (33 * SCALE, 2 * SCALE), (30 * SCALE, 5 * SCALE),
               (33 * SCALE, 8 * SCALE), (21 * SCALE, 8 * SCALE)], fill=(178, 28, 24, 255))
    d.line([22 * SCALE, 3 * SCALE, 29 * SCALE, 3 * SCALE], fill=(206, 60, 48, 255), width=SCALE)

    # outline: a dark edge around everything, so it reads on any backdrop
    alpha = img.split()[3]
    edge = alpha.filter(ImageFilter.MaxFilter(SCALE * 2 + 1))
    outline = Image.new("RGBA", (S, S), (20, 18, 16, 255))
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(outline, (0, 0), edge)
    out.alpha_composite(img)

    small = out.resize((SIZE, SIZE), Image.LANCZOS)
    # hard transparency: icons have one transparent colour, no alpha
    px = small.load()
    for y in range(SIZE):
        for x in range(SIZE):
            rr, gg, bb, aa = px[x, y]
            px[x, y] = (rr, gg, bb, 255) if aa >= 128 else (0, 0, 0, 0)
    return small


def quantize(img, colours):
    """Palette image: index 0 transparent, 1..colours-1 the picture."""
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img, (0, 0), img)
    mask = [img.getpixel((x, y))[3] for y in range(SIZE) for x in range(SIZE)]
    q = rgb.quantize(colours - 1, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = q.getpalette()[:3 * (colours - 1)]
    idx = [(q.getpixel((x, y)) + 1) if mask[y * SIZE + x] else 0 for y in range(SIZE) for x in range(SIZE)]
    palette = [(0, 0, 0)] + [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
    while len(palette) < colours:
        palette.append((0, 0, 0))
    return idx, palette


def rle(values, depth):
    """GlowIcon RLE: a bit stream of 8 bit control codes and depth bit values,
    ByteRun1 style (0..127: n+1 literals, 129..255: repeat 257-n times)."""
    bits = []

    def put(v, n):
        for i in range(n - 1, -1, -1):
            bits.append((v >> i) & 1)

    i = 0
    n = len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            put(257 - run, 8)
            put(values[i], depth)
            i += run
            continue
        j = i
        lit = []
        while j < n and len(lit) < 128:
            if j + 2 < n and values[j] == values[j + 1] == values[j + 2]:
                break
            lit.append(values[j])
            j += 1
        put(len(lit) - 1, 8)
        for v in lit:
            put(v, depth)
        i = j
    while len(bits) % 8:
        bits.append(0)
    return bytes(int("".join(map(str, bits[k:k + 8])), 2) for k in range(0, len(bits), 8))


def chunk(name, data):
    out = name + struct.pack(">I", len(data)) + data
    if len(data) & 1:
        out += b"\0"
    return out


def glowicon(idx, palette):
    depth = max(1, (len(palette) - 1).bit_length())
    face = struct.pack(">BBBBH", SIZE - 1, SIZE - 1, 0, 0x11, len(palette) * 3 - 1)
    image = rle(idx, depth)
    pal = b"".join(struct.pack("BBB", *c) for c in palette)
    imag = struct.pack(">BBBBBBHH",
                       0,                   # transparent colour
                       len(palette) - 1,    # colours - 1
                       3,                   # transparent colour and palette present
                       1,                   # image: RLE
                       0,                   # palette: uncompressed
                       depth,
                       len(image) - 1,
                       len(pal) - 1) + image + pal
    body = b"ICON" + chunk(b"FACE", face) + chunk(b"IMAG", imag)
    return b"FORM" + struct.pack(">I", len(body)) + body


def planar(img):
    """The classic 4 colour image (Workbench grey, black, white, blue)."""
    wb = [(170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187)]
    rgb = Image.new("RGB", img.size, wb[0])
    rgb.paste(img, (0, 0), img)
    pal = Image.new("P", (1, 1))
    pal.putpalette([c for col in wb for c in col] + [0] * (768 - 12))
    q = rgb.quantize(palette=pal, dither=Image.FLOYDSTEINBERG)
    row = ((SIZE + 15) // 16) * 2
    planes = []
    for p in range(2):
        data = bytearray(row * SIZE)
        for y in range(SIZE):
            for x in range(SIZE):
                if img.getpixel((x, y))[3] and (q.getpixel((x, y)) >> p) & 1:
                    data[y * row + x // 8] |= 0x80 >> (x % 8)
        planes.append(bytes(data))
    return b"".join(planes)


def diskobject(classic, form):
    WBTOOL = 3
    gadget = struct.pack(">IhhhhHHHIIIIIHI",
                         0, 0, 0, SIZE, SIZE + 1,
                         0x0004,            # GFLG_GADGIMAGE, complement when selected
                         0x0003,            # RELVERIFY | GADGIMMEDIATE
                         0x0001,            # BOOLGADGET
                         1, 0,              # GadgetRender present, no SelectRender
                         0, 0, 0, 0,
                         1)                 # UserData: revision 1 (OS 2.x icon)
    do = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(">BBIIiiIII",
                     WBTOOL, 0,
                     0, 0,                  # no default tool, no tool types
                     -0x80000000, -0x80000000,  # NO_ICON_POSITION
                     0, 0,
                     2 * 1024 * 1024)       # stack: the game uses its own 2 MB
    image = struct.pack(">hhhhhIBBI", 0, 0, SIZE, SIZE, 2, 1, 3, 0, 0) + classic
    return do + image + form


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "rtcw-sp.info"
    img = draw()
    idx, palette = quantize(img, COLOURS)
    data = diskobject(planar(img), glowicon(idx, palette))
    with open(out, "wb") as f:
        f.write(data)
    if len(sys.argv) > 2:
        prev = Image.new("RGB", (SIZE, SIZE))
        prev.putdata([palette[i] if i else (170, 170, 170) for i in idx])
        prev.resize((SIZE * 4, SIZE * 4), Image.NEAREST).save(sys.argv[2])
    print("%s: %d bytes, %d colours" % (out, len(data), len(palette)))


if __name__ == "__main__":
    main()
