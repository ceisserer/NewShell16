#!/usr/bin/env python3
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate NewShell16's icons (.ico, 32x32x16) and small-image strip (.bmp).

All artwork is drawn procedurally so it can be tweaked and regenerated:

    python3 tools/mkicons.py            # writes into res/

Requires Pillow (only for drawing); the ICO/BMP writers are hand-rolled so the
output is exactly the 4-bit format Windows 3.1 expects.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

# Standard 16-colour VGA palette, in Windows palette-index order.
PAL = [
    (0x00, 0x00, 0x00), (0x80, 0x00, 0x00), (0x00, 0x80, 0x00), (0x80, 0x80, 0x00),
    (0x00, 0x00, 0x80), (0x80, 0x00, 0x80), (0x00, 0x80, 0x80), (0xC0, 0xC0, 0xC0),
    (0x80, 0x80, 0x80), (0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00), (0xFF, 0xFF, 0x00),
    (0x00, 0x00, 0xFF), (0xFF, 0x00, 0xFF), (0x00, 0xFF, 0xFF), (0xFF, 0xFF, 0xFF),
]
BLACK, DRED, DGREEN, OLIVE, NAVY, PURPLE, TEAL, SILVER = range(8)
GRAY, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE = range(8, 16)
T = 16  # transparent marker (not a palette index)


def canvas(n):
    img = Image.new("L", (n, n), T)
    return img, ImageDraw.Draw(img)


def box(d, x0, y0, x1, y1, fill, outline=BLACK):
    d.rectangle([x0, y0, x1, y1], fill=fill, outline=outline)


# --------------------------------------------------------------------------
# Artwork. Each function takes the image size (32 or 16) and returns an image
# whose pixels are palette indices or T.
# --------------------------------------------------------------------------

def folder(n, open_=False):
    img, d = canvas(n)
    s = n / 32
    tab = [round(2 * s), round(6 * s), round(13 * s), round(10 * s)]
    box(d, *tab, fill=YELLOW)
    box(d, round(2 * s), round(9 * s), round(29 * s), round(27 * s), fill=YELLOW)
    d.line([round(3 * s), round(10 * s), round(28 * s), round(10 * s)], fill=WHITE)
    d.line([round(3 * s), round(26 * s), round(28 * s), round(26 * s)], fill=OLIVE)
    d.line([round(28 * s), round(10 * s), round(28 * s), round(26 * s)], fill=OLIVE)
    return img


def document(n):
    img, d = canvas(n)
    s = n / 32
    x0, y0, x1, y1 = round(6 * s), round(2 * s), round(25 * s), round(29 * s)
    fold = round(6 * s)
    d.polygon([(x0, y0), (x1 - fold, y0), (x1, y0 + fold), (x1, y1), (x0, y1)],
              fill=WHITE, outline=BLACK)
    d.line([(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)], fill=BLACK)
    step = max(2, round(3 * s))
    for y in range(y0 + fold + step, y1 - 2, step):
        d.line([x0 + 3, y, x1 - 3, y], fill=GRAY)
    return img


def application(n):
    img, d = canvas(n)
    s = n / 32
    box(d, round(2 * s), round(5 * s), round(29 * s), round(26 * s), fill=WHITE)
    d.rectangle([round(3 * s), round(6 * s), round(28 * s), round(9 * s)], fill=NAVY)
    if n >= 32:
        box(d, 5, 13, 13, 22, fill=SILVER, outline=GRAY)
        box(d, 16, 13, 26, 16, fill=SILVER, outline=GRAY)
        box(d, 16, 19, 26, 22, fill=SILVER, outline=GRAY)
    return img


def computer(n):
    img, d = canvas(n)
    s = n / 32
    # monitor
    box(d, round(4 * s), round(2 * s), round(27 * s), round(20 * s), fill=SILVER)
    box(d, round(7 * s), round(5 * s), round(24 * s), round(17 * s), fill=TEAL)
    d.line([round(8 * s), round(6 * s), round(14 * s), round(6 * s)], fill=CYAN)
    # stand
    d.rectangle([round(12 * s), round(21 * s), round(19 * s), round(22 * s)], fill=GRAY)
    # base unit
    box(d, round(2 * s), round(23 * s), round(29 * s), round(29 * s), fill=SILVER)
    d.line([round(18 * s), round(26 * s), round(26 * s), round(26 * s)], fill=BLACK)
    d.point([round(5 * s), round(26 * s)], fill=GREEN)
    return img


def drive(n, kind):
    img, d = canvas(n)
    s = n / 32
    if kind == "floppy":
        box(d, round(6 * s), round(4 * s), round(25 * s), round(27 * s), fill=NAVY)
        box(d, round(10 * s), round(4 * s), round(21 * s), round(11 * s), fill=SILVER)
        box(d, round(9 * s), round(16 * s), round(22 * s), round(27 * s), fill=WHITE)
        return img
    # hard / cd / net: a drive box in perspective-free style
    box(d, round(2 * s), round(12 * s), round(29 * s), round(23 * s), fill=SILVER)
    d.line([round(3 * s), round(13 * s), round(28 * s), round(13 * s)], fill=WHITE)
    d.line([round(3 * s), round(22 * s), round(28 * s), round(22 * s)], fill=GRAY)
    d.point([round(25 * s), round(19 * s)], fill=GREEN)
    if n >= 32:
        d.point([round(26 * s), round(19 * s)], fill=GREEN)
    if kind == "cdrom":
        d.ellipse([round(6 * s), round(2 * s), round(22 * s), round(18 * s)],
                  fill=SILVER, outline=BLACK)
        d.ellipse([round(12 * s), round(8 * s), round(16 * s), round(12 * s)],
                  fill=T, outline=GRAY)
        d.arc([round(8 * s), round(4 * s), round(20 * s), round(16 * s)], 200, 250, fill=WHITE)
    elif kind == "net":
        d.line([round(15 * s), round(23 * s), round(15 * s), round(28 * s)], fill=BLACK)
        d.line([round(4 * s), round(28 * s), round(27 * s), round(28 * s)], fill=BLACK)
    return img


def bin_(n):
    img, d = canvas(n)
    s = n / 32
    d.polygon([(round(7 * s), round(8 * s)), (round(24 * s), round(8 * s)),
               (round(22 * s), round(29 * s)), (round(9 * s), round(29 * s))],
              fill=SILVER, outline=BLACK)
    box(d, round(5 * s), round(5 * s), round(26 * s), round(8 * s), fill=SILVER)
    box(d, round(13 * s), round(2 * s), round(18 * s), round(5 * s), fill=SILVER)
    for x in (12, 16, 20):
        d.line([round(x * s), round(11 * s), round(x * s), round(26 * s)], fill=GRAY)
    return img


def appicon(n):
    """NewShell's own icon: a desktop with a taskbar and start button."""
    img, d = canvas(n)
    s = n / 32
    box(d, round(1 * s), round(3 * s), round(30 * s), round(28 * s), fill=TEAL)
    d.rectangle([round(2 * s), round(23 * s), round(29 * s), round(27 * s)], fill=SILVER)
    d.rectangle([round(3 * s), round(24 * s), round(9 * s), round(26 * s)], fill=WHITE)
    d.point([round(4 * s), round(25 * s)], fill=RED)
    box(d, round(8 * s), round(7 * s), round(23 * s), round(18 * s), fill=WHITE)
    d.rectangle([round(9 * s), round(8 * s), round(22 * s), round(10 * s)], fill=NAVY)
    return img


def start_logo(n):
    """Small logo for the Start button: a stylised shell (spiral of squares)."""
    img, d = canvas(n)
    s = n / 16
    d.rectangle([round(1 * s), round(2 * s), round(7 * s), round(7 * s)], fill=RED)
    d.rectangle([round(8 * s), round(2 * s), round(14 * s), round(7 * s)], fill=GREEN)
    d.rectangle([round(1 * s), round(8 * s), round(7 * s), round(13 * s)], fill=BLUE)
    d.rectangle([round(8 * s), round(8 * s), round(14 * s), round(13 * s)], fill=YELLOW)
    d.rectangle([round(5 * s), round(5 * s), round(10 * s), round(10 * s)], fill=WHITE,
                outline=BLACK)
    return img


def programs(n):
    img = folder(n)
    d = ImageDraw.Draw(img)
    d.rectangle([7, 7, 14, 13], fill=WHITE, outline=BLACK)
    d.line([8, 8, 13, 8], fill=NAVY)
    return img


def settings(n):
    img, d = canvas(n)
    box(d, 1, 2, 14, 13, fill=SILVER)
    d.line([4, 4, 4, 11], fill=BLACK)
    d.line([8, 4, 8, 11], fill=BLACK)
    d.line([12, 4, 12, 11], fill=BLACK)
    d.rectangle([3, 8, 5, 9], fill=RED)
    d.rectangle([7, 5, 9, 6], fill=BLUE)
    d.rectangle([11, 9, 13, 10], fill=DGREEN)
    return img


def find(n):
    img, d = canvas(n)
    d.ellipse([1, 1, 10, 10], fill=CYAN, outline=BLACK)
    d.point([4, 4], fill=WHITE)
    d.line([9, 9, 14, 14], fill=BLACK, width=2)
    return img


def run(n):
    img = application(n)
    d = ImageDraw.Draw(img)
    d.polygon([(5, 8), (11, 11), (5, 14)], fill=RED, outline=BLACK)
    return img


def shutdown(n):
    img = computer(n)
    d = ImageDraw.Draw(img)
    d.rectangle([4, 3, 11, 8], fill=NAVY)
    d.ellipse([6, 3, 9, 6], fill=YELLOW)
    return img


def help_(n):
    img, d = canvas(n)
    box(d, 2, 1, 13, 14, fill=NAVY)
    d.line([4, 2, 4, 13], fill=BLUE)
    d.text((6, 2), "?", fill=YELLOW)
    return img


def printer(n):
    img, d = canvas(n)
    box(d, 4, 1, 11, 6, fill=WHITE)
    box(d, 1, 6, 14, 12, fill=SILVER)
    box(d, 4, 10, 11, 14, fill=WHITE)
    d.point([12, 8], fill=GREEN)
    return img


def control(n):
    img, d = canvas(n)
    box(d, 1, 1, 14, 14, fill=SILVER)
    d.rectangle([3, 3, 7, 7], fill=YELLOW, outline=BLACK)
    d.rectangle([9, 3, 12, 7], fill=BLUE, outline=BLACK)
    d.rectangle([3, 9, 7, 12], fill=RED, outline=BLACK)
    d.rectangle([9, 9, 12, 12], fill=GREEN, outline=BLACK)
    return img


def about(n):
    img, d = canvas(n)
    d.ellipse([1, 1, 14, 14], fill=WHITE, outline=NAVY)
    d.rectangle([7, 6, 8, 11], fill=NAVY)
    d.rectangle([7, 3, 8, 4], fill=NAVY)
    return img


def dos(n):
    img, d = canvas(n)
    box(d, 1, 2, 14, 13, fill=BLACK, outline=GRAY)
    d.line([3, 5, 5, 7], fill=WHITE)
    d.line([5, 7, 3, 9], fill=WHITE)
    d.line([7, 10, 11, 10], fill=WHITE)
    return img


def bin_full(n):
    img = bin_(n)
    d = ImageDraw.Draw(img)
    s = n / 32
    d.polygon([(round(8 * s), round(8 * s)), (round(13 * s), round(1 * s)),
               (round(22 * s), round(4 * s)), (round(23 * s), round(8 * s))],
              fill=WHITE, outline=BLACK)
    d.line([round(12 * s), round(4 * s), round(19 * s), round(6 * s)], fill=GRAY)
    return img


def explorer(n):
    """A folder with a magnifying tree: folder plus a small tree diagram."""
    img, d = canvas(n)
    s = n / 32
    box(d, round(1 * s), round(2 * s), round(30 * s), round(29 * s), fill=WHITE)
    d.rectangle([round(2 * s), round(3 * s), round(29 * s), round(6 * s)], fill=NAVY)
    d.line([round(11 * s), round(7 * s), round(11 * s), round(28 * s)], fill=GRAY)
    for y in (10, 15, 20):
        d.rectangle([round(3 * s), round(y * s), round(8 * s), round((y + 3) * s)],
                    fill=YELLOW, outline=OLIVE)
    for y in (10, 14, 18, 22):
        d.rectangle([round(14 * s), round(y * s), round(17 * s), round((y + 2) * s)],
                    fill=YELLOW, outline=OLIVE)
        d.line([round(19 * s), round((y + 1) * s), round(27 * s), round((y + 1) * s)],
               fill=BLACK)
    return img


def taskman(n):
    img, d = canvas(n)
    s = n / 32
    box(d, round(2 * s), round(4 * s), round(29 * s), round(27 * s), fill=SILVER)
    d.rectangle([round(3 * s), round(5 * s), round(28 * s), round(8 * s)], fill=NAVY)
    box(d, round(5 * s), round(11 * s), round(26 * s), round(23 * s), fill=WHITE)
    for i, y in enumerate((13, 17, 21)):
        d.rectangle([round(7 * s), round(y * s), round(9 * s), round((y + 1) * s)],
                    fill=(RED, GREEN, BLUE)[i])
        d.line([round(12 * s), round(y * s), round(24 * s), round(y * s)], fill=BLACK)
    return img


def find_icon(n):
    img = document(n)
    d = ImageDraw.Draw(img)
    s = n / 32
    d.ellipse([round(9 * s), round(9 * s), round(23 * s), round(23 * s)],
              fill=CYAN, outline=BLACK, width=max(1, round(2 * s)))
    d.line([round(21 * s), round(21 * s), round(29 * s), round(29 * s)],
           fill=BLACK, width=max(2, round(3 * s)))
    return img


def bin_small(n):
    return bin_(n)


def explorer_small(n):
    img, d = canvas(n)
    box(d, 0, 1, 15, 14, fill=WHITE)
    d.rectangle([1, 2, 14, 3], fill=NAVY)
    d.line([5, 4, 5, 13], fill=GRAY)
    d.rectangle([1, 5, 3, 7], fill=YELLOW, outline=OLIVE)
    d.rectangle([7, 6, 9, 8], fill=YELLOW, outline=OLIVE)
    d.rectangle([7, 10, 9, 12], fill=YELLOW, outline=OLIVE)
    d.line([11, 7, 13, 7], fill=BLACK)
    d.line([11, 11, 13, 11], fill=BLACK)
    return img


def taskman_small(n):
    img, d = canvas(n)
    box(d, 0, 1, 15, 14, fill=SILVER)
    d.rectangle([1, 2, 14, 3], fill=NAVY)
    box(d, 2, 5, 13, 12, fill=WHITE)
    d.point([4, 7], fill=RED)
    d.point([4, 10], fill=GREEN)
    d.line([6, 7, 11, 7], fill=BLACK)
    d.line([6, 10, 11, 10], fill=BLACK)
    return img


# --------------------------------------------------------------------------
# Cursors (monochrome 32x32): '.' transparent, '#' black, 'o' white
# --------------------------------------------------------------------------

ARROW = [
    "#",
    "##",
    "#o#",
    "#oo#",
    "#ooo#",
    "#oooo#",
    "#ooooo#",
    "#oooooo#",
    "#ooooooo#",
    "#oooooooo#",
    "#ooooo#####",
    "#oo#oo#",
    "#o# #oo#",
    "##  #oo#",
    "#    #oo#",
    "     #oo#",
    "      ##",
]


def cursor_canvas():
    return [["." for _ in range(32)] for _ in range(32)]


def stamp(c, rows, x0, y0):
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in "#o":
                c[y0 + y][x0 + x] = ch


def drag_cursor(plus):
    c = cursor_canvas()
    stamp(c, ARROW, 0, 0)
    page = ["##########", "#oooooooo#", "#o######o#", "#oooooooo#", "#o######o#",
            "#oooooooo#", "#o####ooo#", "#oooooooo#", "##########"]
    stamp(c, page, 9, 15)
    if plus:
        stamp(c, ["#####", "#ooo#", "#ooo#", "#ooo#", "#####"], 20, 19)
        stamp(c, ["  #  ", "  #  ", "#####", "  #  ", "  #  "], 20, 19)
    return c, (0, 0)


def nodrop_cursor():
    c = cursor_canvas()
    for y in range(32):
        for x in range(32):
            r = ((x - 12) ** 2 + (y - 12) ** 2) ** 0.5
            on_slash = abs((x - 12) - (y - 12)) <= 2 and r < 10
            if 8 <= r <= 11 or on_slash:
                c[y][x] = "#"
            elif 7 <= r < 8 or 11 < r <= 12 or (abs((x - 12) - (y - 12)) == 3 and r < 9):
                if c[y][x] == ".":
                    c[y][x] = "o"
    return c, (12, 12)


def split_cursor():
    c = cursor_canvas()
    rows = [
        "    ##  ##    ",
        "    #o##o#    ",
        "  # #o##o# #  ",
        " ## #o##o# ## ",
        "#o###o##o###o#",
        "#oooooooooooo#",
        "#o###o##o###o#",
        " ## #o##o# ## ",
        "  # #o##o# #  ",
        "    #o##o#    ",
        "    ##  ##    ",
    ]
    stamp(c, rows, 9, 10)
    return c, (16, 15)


def write_cur(path, cur):
    c, (hx, hy) = cur
    xor_rows, and_rows = [], []
    for y in reversed(range(32)):
        xor_rows.append(pack1([1 if c[y][x] == "o" else 0 for x in range(32)]))
        and_rows.append(pack1([1 if c[y][x] == "." else 0 for x in range(32)]))
    xor, and_ = b"".join(xor_rows), b"".join(and_rows)
    pal = struct.pack("<BBBBBBBB", 0, 0, 0, 0, 255, 255, 255, 0)
    bih = struct.pack("<IiiHHIIiiII", 40, 32, 64, 1, 1, 0, len(xor) + len(and_), 0, 0, 2, 0)
    data = bih + pal + xor + and_
    hdr = struct.pack("<HHH", 0, 2, 1)
    entry = struct.pack("<BBBBHHII", 32, 32, 2, 0, hx, hy, len(data), 6 + 16)
    with open(path, "wb") as f:
        f.write(hdr + entry + data)


# --------------------------------------------------------------------------
# Writers
# --------------------------------------------------------------------------

def palette_bytes():
    return b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in PAL)


def pack4(row):
    out = bytearray()
    for i in range(0, len(row), 2):
        out.append((row[i] << 4) | row[i + 1])
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def pack1(row):
    out = bytearray()
    for i in range(0, len(row), 8):
        b = 0
        for j, v in enumerate(row[i:i + 8]):
            if v:
                b |= 0x80 >> j
        out.append(b)
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def write_ico(path, img):
    w, h = img.size
    px = img.load()
    xor = b"".join(pack4([0 if px[x, y] == T else px[x, y] for x in range(w)])
                   for y in reversed(range(h)))
    and_ = b"".join(pack1([1 if px[x, y] == T else 0 for x in range(w)])
                    for y in reversed(range(h)))
    bih = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 4, 0, len(xor) + len(and_), 0, 0, 16, 0)
    data = bih + palette_bytes() + xor + and_
    hdr = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", w, h, 16, 0, 1, 4, len(data), 6 + 16)
    with open(path, "wb") as f:
        f.write(hdr + entry + data)


def write_bmp(path, img, key=MAGENTA):
    """4-bit BMP; transparent pixels are written as MAGENTA (the colour key)."""
    w, h = img.size
    px = img.load()
    bits = b"".join(pack4([key if px[x, y] == T else px[x, y] for x in range(w)])
                    for y in reversed(range(h)))
    bih = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0, len(bits), 0, 0, 16, 0)
    off = 14 + len(bih) + 64
    fh = struct.pack("<2sIHHI", b"BM", off + len(bits), 0, 0, off)
    with open(path, "wb") as f:
        f.write(fh + bih + palette_bytes() + bits)


# Order must match the SMI_* constants in src/resource.h.
SMALL = [
    ("programs", programs), ("document", document), ("settings", settings),
    ("find", find), ("run", run), ("shutdown", shutdown), ("computer", computer),
    ("folder", folder), ("help", help_), ("printer", printer), ("control", control),
    ("about", about), ("dos", dos), ("application", application), ("start", start_logo),
    ("explorer", explorer_small), ("bin", bin_small), ("taskman", taskman_small),
]

ICONS = [
    ("newshell", appicon), ("folder", folder), ("document", document),
    ("app", application), ("computer", computer),
    ("floppy", lambda n: drive(n, "floppy")), ("hdd", lambda n: drive(n, "hard")),
    ("cdrom", lambda n: drive(n, "cdrom")), ("netdrv", lambda n: drive(n, "net")),
    ("bin", bin_), ("binfull", bin_full), ("explorer", explorer),
    ("taskman", taskman), ("find", find_icon),
]

CURSORS = [
    ("dragmove", lambda: drag_cursor(False)), ("dragcopy", lambda: drag_cursor(True)),
    ("nodrop", nodrop_cursor), ("split", split_cursor),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "res")
    os.makedirs(out, exist_ok=True)
    for name, fn in ICONS:
        write_ico(os.path.join(out, name + ".ico"), fn(32))
    strip = Image.new("L", (16 * len(SMALL), 16), T)
    for i, (_, fn) in enumerate(SMALL):
        strip.paste(fn(16), (16 * i, 0))
    write_bmp(os.path.join(out, "small.bmp"), strip)
    for name, fn in CURSORS:
        write_cur(os.path.join(out, name + ".cur"), fn())


if __name__ == "__main__":
    main()
