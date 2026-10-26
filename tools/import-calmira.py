#!/usr/bin/env python3
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy the artwork NewShell16 uses from a Calmira source tree into res/calmira/.

    git clone https://github.com/RauliL/calmira
    python3 tools/import-calmira.py calmira

Icons come from SRC/ICONS.RES (a 16-bit resource file), the small images
from the bitmaps in BMP/. Only the images NewShell16 uses are copied;
images showing third-party logos are left out. tools/mkicons.py then
builds res/*.ico and res/small.bmp from them. The result is committed, so
this is only needed to pick up more of Calmira's artwork.
"""
import os
import struct
import sys

from PIL import Image

# ICONS.RES group name -> file name in res/calmira
ICONS = {
    "BINICON": "bin", "FULLBINICON": "binfull", "CDROMICON": "cdrom",
    "COMPUTERICON": "computer", "EXPLORERICON": "explorer", "FINDDLGICON": "find",
    "FLOPPYICON": "floppy", "FOLDERICON": "folder", "HARDICON": "hdd",
    "NETWORKICON": "netdrv", "PROGICON": "app", "TASKMANICON": "taskman",
    "FILEICON": "document", "SMDOCICON": "sm_documents",
    "SMFAVICON": "sm_favorites", "SMFINDICON": "sm_find", "SMGROUPICON": "sm_programs",
    "SMHELPICON": "sm_help", "SMRUNICON": "sm_run", "SMSETTINGSICON": "sm_settings",
    "SMSHUTDOWNICON": "sm_shutdown", "INTERNETICON": "sm_internet",
}

# The Start menu's preset images, 16x16 each in BMP/STARTDEF.BMP
STARTDEF = ["", "programs", "folder", "find", "run", "explorer", "shutdown",
            "settings", "help", "documents", "favorites", "internet", "system",
            "bin", "", "", "control"]

# Single 16x16 bitmaps in BMP/
BITMAPS = ["PARENT", "MKDIR", "CUT", "COPY", "PASTE", "DELETE", "PROP", "LARGE",
           "SMALL", "LIST", "DETAILS", "SORTNAME", "SORTTYPE", "SORTSIZE",
           "SORTDATE", "PRINT", "SPLEAF"]


def read_res(path):
    """Yields (type, name, data) for each resource of a 16-bit .RES file."""
    d = open(path, "rb").read()
    o = 0
    while o < len(d):
        ids = []
        for _ in range(2):
            if d[o] == 0xFF:
                ids.append(struct.unpack_from("<H", d, o + 1)[0])
                o += 3
            else:
                e = d.index(b"\0", o)
                ids.append(d[o:e].decode("latin-1"))
                o = e + 1
        _, size = struct.unpack_from("<HI", d, o)
        o += 6
        yield ids[0], ids[1], d[o:o + size]
        o += size


def write_icons(res, out):
    images = {name: data for typ, name, data in res if typ == 3}
    for typ, name, data in res:
        if typ != 14 or name not in ICONS:
            continue
        count = struct.unpack_from("<H", data, 4)[0]
        entries, blobs = [], []
        for i in range(count):
            w, h, c, r, planes, bits, _, ident = struct.unpack_from("<BBBBHHIH", data, 6 + 14 * i)
            entries.append((w, h, c, r, planes, bits))
            blobs.append(images[ident])
        offset = 6 + 16 * count
        head = struct.pack("<HHH", 0, 1, count)
        for (w, h, c, r, planes, bits), blob in zip(entries, blobs):
            head += struct.pack("<BBBBHHII", w, h, c, r, planes, bits, len(blob), offset)
            offset += len(blob)
        with open(os.path.join(out, ICONS[name] + ".ico"), "wb") as f:
            f.write(head + b"".join(blobs))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "calmira")
    os.makedirs(out, exist_ok=True)
    write_icons(list(read_res(os.path.join(src, "SRC", "ICONS.RES"))), out)
    strip = Image.open(os.path.join(src, "BMP", "STARTDEF.BMP"))
    for i, name in enumerate(STARTDEF):
        if name:
            strip.crop((16 * i, 0, 16 * i + 16, 16)).save(os.path.join(out, "sm_" + name + ".bmp"))
    for name in BITMAPS:
        Image.open(os.path.join(src, "BMP", name + ".BMP")).save(
            os.path.join(out, name.lower() + ".bmp"))


if __name__ == "__main__":
    main()
