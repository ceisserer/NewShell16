#!/usr/bin/env python3
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a static web page that runs Windows 3.1 with NewShell16 in PCjs.

  mkpcjs.py [--out DIR] [--image IMG] [--build] [--theme NAME|none]
            [--no-newshell] [--drive-type N] [--mhz N] [--ram KB]

The result is a directory for any static web server (no server-side code):

  index.html      the machine: an IBM PC AT (80286), IBM VGA, 4 MB RAM
  pcx86.js        the PCjs PCx86 emulator (MIT licence)
  *.css           PCjs's machine styles
  rom/*.json5     IBM PC AT BIOS and IBM VGA BIOS (from the PCjs repository)
  win31.img       the hard disk: MS-DOS 6.20, Windows 3.1 and NewShell16

Steps:

 1. tools/qemu/mkimage.sh installs MS-DOS and Windows 3.1 (VGA, 16 colours)
    into a QEMU image with the NewShell16 setup files in C:\\NSSETUP (or
    --image takes an image made by it).
 2. NewShell16 Setup runs silently in QEMU (SETUP /S), as the shell, with
    the bundled Retheme16 look (--theme, default WIN95).
 3. The FAT16 volume is moved onto an IBM PC AT drive type that PCjs
    knows (--drive-type, default 2: 615 cylinders, 4 heads, 17 sectors,
    20 MB), with the MS-DOS boot code kept, and set up for a 286.
 4. PCjs, its styles and the two ROMs are downloaded from the PCjs
    repository on GitHub at a fixed commit (cached in
    ~/.cache/newshell16/pcjs) and the page is written.

The page and the disk image contain licensed Microsoft software: publish
them only where you may. Nothing of it belongs in this repository.

Needs what tools/qemu/mkimage.sh needs (qemu-system-i386, 7z, mtools,
sfdisk, unzip, curl, git, Python 3) and the installation media it lists
(MEDIA=DIR or next to tools/qemu/mkimage.sh), plus mkfs.fat (dosfstools).
"""
import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MKIMAGE = os.path.join(ROOT, "tools", "qemu", "mkimage.sh")

# PCjs (https://github.com/jeffpar/pcjs) at a fixed commit: PCx86 2.26
PCJS_REF = "521111206f2d83e645c3cd6d1c19201b3d32f061"
PCJS_RAW = "https://raw.githubusercontent.com/jeffpar/pcjs/%s/"
PCJS_FILES = {
    # published name: path in the PCjs repository
    "pcx86.js": "machines/pcx86/releases/2.26/pcx86.js",
    "components.css": "machines/pcx86/xsl/components.css",
    "rom/ATBIOS-REV3.json5": "machines/pcx86/ibm/5170/rom/bios/1985-11-15/ATBIOS-REV3.json5",
    "rom/IBM-VGA.json5": "machines/pcx86/ibm/video/vga/1986-10-27/IBM-VGA.json5",
}

# IBM PC AT drive types as PCjs and the AT BIOS (rev 3) define them:
# type: (cylinders, heads); 17 sectors per track
AT_DRIVE_TYPES = {
    1: (306, 4), 2: (615, 4), 3: (615, 6), 4: (940, 8), 5: (940, 6),
    7: (462, 8), 8: (733, 5), 9: (900, 15), 10: (820, 3), 11: (855, 5),
    12: (855, 7), 13: (306, 8), 14: (733, 7), 16: (612, 4), 17: (977, 5),
    18: (977, 7), 19: (1024, 7), 20: (733, 5), 21: (733, 7), 22: (733, 5),
}
SPT = 17
QEMU_PART = 63 * 512          # mkimage.sh images: partition at sector 63

AUTOEXEC = "@ECHO OFF\r\nPATH C:\\WINDOWS;C:\\\r\nSET TEMP=C:\\WINDOWS\\TEMP\r\n%s\r\n"


def log(msg):
    print("== " + msg, flush=True)


def run(*args, **kw):
    return subprocess.run(list(args), check=kw.pop("check", True), capture_output=True,
                          encoding="latin-1", **kw)


def img(path, off):
    return "%s@@%d" % (path, off)


def put_text(image, text, dest):
    with tempfile.NamedTemporaryFile("w", delete=False, newline="", encoding="latin-1") as f:
        f.write(text)
    run("mcopy", "-o", "-i", image, f.name, dest)
    os.unlink(f.name)


def get_text(image, src):
    return run("mtype", "-i", image, src).stdout


# ---------------------------------------------------------------- step 1

def base_image(a, work):
    if a.image:
        log("base image: %s" % a.image)
        shutil.copy(a.image, os.path.join(work, "base.img"))
        return os.path.join(work, "base.img")
    log("installing MS-DOS and Windows 3.1 with tools/qemu/mkimage.sh")
    out = os.path.join(work, "base.img")
    cmd = [MKIMAGE, "-o", out, "-d", "4"]
    if a.build:
        cmd.append("-b")
    subprocess.run(cmd, check=True)
    return out


# ---------------------------------------------------------------- step 2

def qemu_monitor(sock, command):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    s.settimeout(2)
    s.sendall((command + "\n").encode())
    time.sleep(0.5)
    s.close()


def check_vga(image):
    """PCjs emulates an IBM VGA, nothing beyond: no SVGA, no VBE."""
    text = get_text(img(image, QEMU_PART), "::/WINDOWS/SYSTEM.INI")
    sec = None
    for line in text.splitlines():
        if line.strip().startswith("["):
            sec = line.strip().lower()
            continue
        k, _, v = line.partition("=")
        if sec == "[boot]" and k.strip().lower() == "display.drv" and v.strip().lower() != "vga.drv":
            sys.exit("the image uses %s; PCjs needs vga.drv (mkimage.sh -d 4)" % v.strip())


def install_newshell(a, image, work):
    c = img(image, QEMU_PART)
    if run("mdir", "-i", c, "::/NSSETUP/SETUP.EXE", check=False).returncode:
        sys.exit("no C:\\NSSETUP\\SETUP.EXE in the image (made by tools/qemu/mkimage.sh?)")
    opts = "/S /NOTHEME" if a.theme.lower() == "none" else "/S /T=%s" % a.theme.upper()
    log("installing NewShell16 silently in QEMU (SETUP %s)" % opts)
    autoexec = get_text(c, "::/AUTOEXEC.BAT")
    put_text(c, AUTOEXEC % ("WIN C:\\NSSETUP\\SETUP.EXE " + opts), "::/AUTOEXEC.BAT")
    sock = os.path.join(work, "mon.sock")
    q = subprocess.Popen(["qemu-system-i386", "-m", "16", "-cpu", "pentium", "-display", "none",
                          "-drive", "file=%s,if=ide,format=raw" % image,
                          "-monitor", "unix:%s,server,nowait" % sock],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = False
    try:
        start = time.time()
        while time.time() - start < a.timeout:
            time.sleep(3)
            if run("mdir", "-i", c, "::/NEWSHELL/SETUP.LOG", check=False).returncode == 0:
                # Setup writes its log last; let Program Manager finish the group
                time.sleep(10)
                ok = True
                break
    finally:
        if not ok and os.path.exists(sock):
            try:
                qemu_monitor(sock, "screendump %s" % os.path.join(a.out, "setup-fail.ppm"))
            except OSError:
                pass
        q.kill()
        q.wait()
    if not ok:
        sys.exit("NewShell16 Setup did not finish (screen: %s/setup-fail.ppm)" % a.out)
    setup_log = get_text(c, "::/NEWSHELL/SETUP.LOG")
    print(setup_log.replace("\r", ""))
    if "has been installed" not in setup_log:
        sys.exit("NewShell16 Setup failed (see SETUP.LOG above)")
    put_text(c, autoexec, "::/AUTOEXEC.BAT")


# ---------------------------------------------------------------- step 3

def chs(lba, heads):
    c, rest = divmod(lba, heads * SPT)
    h, s = divmod(rest, SPT)
    return bytes([h, ((s + 1) & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF])


def regeometry(src, dst, drive_type, work, drop):
    """Copies the FAT16 volume of a mkimage.sh image (CHS x/16/63, partition
    at sector 63) onto a new disk with the geometry of an AT drive type."""
    cyls, heads = AT_DRIVE_TYPES[drive_type]
    total = cyls * heads * SPT
    start = SPT                                   # cylinder 0, head 1, sector 1
    size = total - start
    log("disk: AT drive type %d, %d cylinders, %d heads, %d sectors (%.1f MB)"
        % (drive_type, cyls, heads, SPT, total * 512 / 1e6))

    with open(src, "rb") as f:
        mbr = f.read(512)
        f.seek(QEMU_PART)
        boot = f.read(512)
    if mbr[510:] != b"\x55\xaa" or boot[510:] != b"\x55\xaa" or boot[0x36:0x3B] != b"FAT16":
        sys.exit("%s: not a FAT16 disk made by mkimage.sh" % src)

    # files of the old volume, with upper-case names (no VFAT long names)
    files = os.path.join(work, "files")
    shutil.rmtree(files, ignore_errors=True)
    os.makedirs(files)
    run("mcopy", "-s", "-m", "-n", "-i", img(src, QEMU_PART), "::/*", files)
    for d, dirs, names in os.walk(files, topdown=False):
        for n in names + dirs:
            if n != n.upper():
                os.rename(os.path.join(d, n), os.path.join(d, n.upper()))
    for n in drop:
        shutil.rmtree(os.path.join(files, n), ignore_errors=True)

    # the new volume: mkfs.fat's BPB for this geometry, MS-DOS's boot code
    part = os.path.join(work, "part.img")
    with open(part, "wb") as f:
        f.truncate(size * 512)
    run("mkfs.fat", "-F", "16", "-g", "%d/%d" % (heads, SPT), "-h", str(start),
        "-M", "0xF8", "-D", "0x80", "-i", "19920406", part)
    with open(part, "r+b") as f:
        new = bytearray(f.read(512))
        new[3:11] = boot[3:11]                    # OEM name (MSDOS5.0)
        new[0x3E:0x1FE] = boot[0x3E:0x1FE]        # boot code
        f.seek(0)
        f.write(new)

    # MS-DOS's boot code wants IO.SYS and MSDOS.SYS as the first two root
    # directory entries, IO.SYS contiguous: copy them first, into a fresh volume
    sysfiles = ["IO.SYS", "MSDOS.SYS"]
    for n in sysfiles:
        if not os.path.exists(os.path.join(files, n)):
            sys.exit("%s has no %s" % (src, n))
        run("mcopy", "-m", "-i", part, os.path.join(files, n), "::/" + n)
    rest = sorted(n for n in os.listdir(files) if n not in sysfiles)
    run("mcopy", "-s", "-m", "-i", part, *[os.path.join(files, n) for n in rest], "::/")
    run("mattrib", "-i", part, "+r", "+s", "+h", "::/IO.SYS", "::/MSDOS.SYS")
    if os.path.exists(os.path.join(files, "DBLSPACE.BIN")):
        run("mattrib", "-i", part, "+r", "+s", "+h", "::/DBLSPACE.BIN")

    # the disk: MS-DOS's master boot code, one active FAT16 partition
    entry = (b"\x80" + chs(start, heads) + bytes([0x04 if size < 65536 else 0x06])
             + chs(total - 1, heads) + struct.pack("<II", start, size))
    with open(dst, "wb") as f:
        f.write(mbr[:446] + entry + bytes(48) + b"\x55\xaa")
        f.write(bytes((start - 1) * 512))
        with open(part, "rb") as p:
            shutil.copyfileobj(p, f)
    if os.path.getsize(dst) != total * 512:
        sys.exit("internal error: %s has %d bytes, not %d" % (dst, os.path.getsize(dst), total * 512))
    os.unlink(part)
    shutil.rmtree(files)
    return start * 512


def setup_286(image, off):
    """Boot files for a 286 with 4 MB: Windows in standard mode."""
    c = img(image, off)
    put_text(c, "DEVICE=C:\\WINDOWS\\HIMEM.SYS\r\nFILES=40\r\nBUFFERS=20\r\n", "::/CONFIG.SYS")
    put_text(c, AUTOEXEC % "WIN /S", "::/AUTOEXEC.BAT")
    # PCjs has a serial mouse on COM1, which MOUSE.DRV ("Microsoft, or IBM
    # PS/2") finds by itself; the pointer is relative as in QEMU, so keep
    # the acceleration that mkimage.sh switched off (MouseSpeed=0)
    for n in ("CONFIG.OLD", "AUTOEXEC.OLD"):
        run("mdel", "-i", c, "::/" + n, check=False)


# ---------------------------------------------------------------- step 4

def fetch_pcjs(ref, out):
    cache = os.path.join(os.path.expanduser("~"), ".cache", "newshell16", "pcjs", ref)
    for name, path in PCJS_FILES.items():
        cached = os.path.join(cache, path)
        if not os.path.exists(cached):
            os.makedirs(os.path.dirname(cached), exist_ok=True)
            url = PCJS_RAW % ref + path
            log("downloading %s" % url)
            with urllib.request.urlopen(url) as r, open(cached + ".part", "wb") as f:
                shutil.copyfileobj(r, f)
            os.rename(cached + ".part", cached)
        dest = os.path.join(out, name)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy(cached, dest)


def write_page(a, out, newshell):
    cyls, heads = AT_DRIVE_TYPES[a.drive_type]
    mb = cyls * heads * SPT * 512 / (1024 * 1024)
    ext = a.ram - 640
    with open(os.path.join(HERE, "index.html.in"), encoding="utf-8") as f:
        page = f.read()
    title = "Windows 3.1 with NewShell16" if newshell else "Windows 3.1"
    for k, v in {
        "TITLE": title,
        "MACHINE": "IBM PC AT (80286, %d MHz), %s RAM, IBM VGA, %d MB hard disk"
                   % (a.mhz, ("%d MB" % (a.ram // 1024)) if a.ram % 1024 == 0 else "%d KB" % a.ram, round(mb)),
        "RAMEXT": "0x%x" % (ext * 1024),
        "CYCLES": str(a.mhz * 1000000),
        "DRIVES": "[{name:&quot;%d Mb Hard Disk&quot;,type:%d,path:&quot;win31.img&quot;}]"
                  % (round(mb), a.drive_type),
    }.items():
        page = page.replace("@%s@" % k, v)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "pcjs"), help="output directory")
    ap.add_argument("--image", help="an image made by tools/qemu/mkimage.sh -d 4 (default: make one)")
    ap.add_argument("--build", action="store_true", help="mkimage.sh -b: build NewShell16 from source")
    ap.add_argument("--theme", default="WIN95", help="Retheme16 theme for NewShell16 Setup, or none")
    ap.add_argument("--no-newshell", action="store_true",
                    help="keep Program Manager; leave the setup files in C:\\NSSETUP")
    ap.add_argument("--drive-type", type=int, default=2, choices=sorted(AT_DRIVE_TYPES),
                    help="IBM PC AT drive type of the hard disk (default 2: 20 MB)")
    ap.add_argument("--mhz", type=int, default=12, help="CPU clock (default 12)")
    ap.add_argument("--ram", type=int, default=4096, help="memory in KB, 1024..15360 (default 4096)")
    ap.add_argument("--pcjs-ref", default=PCJS_REF, help="PCjs git commit to take PCjs and the ROMs from")
    ap.add_argument("--timeout", type=int, default=600, help="seconds for NewShell16 Setup in QEMU")
    a = ap.parse_args()
    if not 1024 <= a.ram <= 15360 or a.ram % 64:
        ap.error("--ram: 1024..15360, a multiple of 64")
    missing = [t for t in ("qemu-system-i386", "mcopy", "mkfs.fat") if not shutil.which(t)]
    if missing:
        sys.exit("missing tools: %s (see %s)" % (" ".join(missing), __file__))

    a.out = os.path.abspath(a.out)
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="mkpcjs-")
    try:
        base = base_image(a, work)
        check_vga(base)
        newshell = not a.no_newshell
        if newshell:
            install_newshell(a, base, work)
        disk = os.path.join(a.out, "win31.img")
        off = regeometry(base, disk, a.drive_type, work,
                         ["NSSETUP", "RTSETUP"] if newshell else ["RTSETUP"])
        setup_286(disk, off)
        fetch_pcjs(a.pcjs_ref, a.out)
        write_page(a, a.out, newshell)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    free = run("mdir", "-i", img(disk, off), "::/").stdout.strip().splitlines()[-1].strip()
    log("%s ready (C: %s); serve it with e.g. python3 -m http.server -d %s" % (a.out, free, a.out))


if __name__ == "__main__":
    main()
