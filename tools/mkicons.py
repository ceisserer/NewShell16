#!/usr/bin/env python3
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate NewShell16's icons (.ico, 32x32x16) and small-image strip (.bmp).

Most artwork is Calmira's, taken from res/calmira/ (see
tools/import-calmira.py); the rest is drawn procedurally here. To regenerate:

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
# Calmira's artwork
# --------------------------------------------------------------------------

CALMIRA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "calmira")


def calmira(name, blank=None):
    """An image from res/calmira: name.ico for size 32, name.bmp for 16 (the
    colour of its bottom-left pixel is transparent, as in Calmira). blank is
    a rectangle to paint white."""
    def load(n):
        img, d = canvas(n)
        src = Image.open(os.path.join(CALMIRA, name + (".ico" if n == 32 else ".bmp")))
        src = src.convert("RGBA")
        key = None if src.format == "ICO" else src.getpixel((0, src.height - 1))
        x0, y0 = (n - src.width) // 2, (n - src.height) // 2
        for y in range(src.height):
            for x in range(src.width):
                p = src.getpixel((x, y))
                if p[3] and p != key:
                    img.putpixel((x0 + x, y0 + y), PAL.index(p[:3]))
        if blank:
            d.rectangle(blank, fill=WHITE)
        return img
    return load


# --------------------------------------------------------------------------
# NewShell16's own artwork, for images Calmira has no counterpart for. Each
# function takes the image size (32 or 16) and returns an image whose pixels
# are palette indices or T.
# --------------------------------------------------------------------------

def appicon(n):
    """NewShell16's own icon: a desktop with a taskbar and start button."""
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
    ("programs", calmira("sm_programs")), ("document", calmira("sm_documents")),
    ("settings", calmira("sm_settings")), ("find", calmira("sm_find")),
    ("run", calmira("sm_run")), ("shutdown", calmira("sm_shutdown")),
    ("computer", calmira("sm_system")), ("folder", calmira("sm_folder")),
    ("help", calmira("sm_help")), ("printer", calmira("print")),
    ("control", calmira("sm_control")), ("about", about), ("dos", dos),
    ("application", calmira("spleaf")), ("start", start_logo),
    ("explorer", calmira("sm_explorer")), ("bin", calmira("sm_bin")),
    ("taskman", taskman_small),
    ("tb_up", calmira("parent")), ("tb_newfolder", calmira("mkdir")),
    ("tb_cut", calmira("cut")), ("tb_copy", calmira("copy")),
    ("tb_paste", calmira("paste")), ("tb_delete", calmira("delete")),
    ("tb_props", calmira("prop")), ("tb_large", calmira("large")),
    ("tb_small", calmira("small")), ("tb_list", calmira("list")),
    ("tb_details", calmira("details")), ("tb_sortname", calmira("sortname")),
    ("tb_sorttype", calmira("sorttype")), ("tb_sortsize", calmira("sortsize")),
    ("tb_sortdate", calmira("sortdate")), ("favorites", calmira("sm_favorites")),
    ("internet", calmira("sm_internet")),
]

# Order must match IDI_* in src/resource.h (newshell.rc names the files).
ICONS = [
    ("newshell", appicon), ("folder", calmira("folder")),
    # Calmira's file icon without the logo on it
    ("document", calmira("document", blank=(7, 10, 24, 23))),
    ("app", calmira("app")), ("computer", calmira("computer")),
    ("floppy", calmira("floppy")), ("hdd", calmira("hdd")),
    ("cdrom", calmira("cdrom")), ("netdrv", calmira("netdrv")),
    ("bin", calmira("bin")), ("binfull", calmira("binfull")),
    ("explorer", calmira("explorer")), ("taskman", calmira("taskman")),
    ("find", calmira("find")),
    # large Start menu images
    ("sm_programs", calmira("sm_programs")), ("sm_documents", calmira("sm_documents")),
    ("sm_settings", calmira("sm_settings")), ("sm_find", calmira("sm_find")),
    ("sm_run", calmira("sm_run")), ("sm_shutdown", calmira("sm_shutdown")),
    ("sm_help", calmira("sm_help")), ("sm_favorites", calmira("sm_favorites")),
    ("sm_internet", calmira("sm_internet")),
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
