#!/usr/bin/env python3
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and packages a NewShell16 release with Win16Setup.

  mkrelease.py [--version X.Y.Z] [--out build/release] [--no-floppy]

Builds NEWSHELL.EXE / NSHOOK.DLL / NSDLG.DLL (wmake), LFNTSR.COM
(external/lfntsr, NASM), Win16Setup's SETUP.EXE (external/win16setup) and Retheme16 (external/retheme16: RETHEME.EXE,
RETHEME.DLL, THEMES.EXE, GALLERY.EXE and its host previewer for the theme previews),
stages the installation files and lets Win16Setup's packer write:

  newshell-X.Y.Z/             the installation files (with SETUP.EXE)
  newshell-X.Y.Z.zip          unzip, run SETUP.EXE
  newshell-X.Y.Z-disk.img     1.44 MB floppy image with the same files
  SHA256SUMS

Needs Open Watcom 2.0 (WATCOM, default /opt/watcom), NASM, gcc, Python 3
with Pillow and mtools. RETHEME16_DIR=<dir> in the environment bundles another
Retheme16 checkout instead of the submodule. The ZIP and the floppy image are reproducible: file
dates come from SOURCE_DATE_EPOCH (default: the last git commit).
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W16SETUP = os.path.join(ROOT, "external", "win16setup")
# RETHEME16_DIR selects another Retheme16 checkout (e.g. a branch with new themes)
RETHEME = os.path.abspath(os.environ.get("RETHEME16_DIR") or os.path.join(ROOT, "external", "retheme16"))
PACK = os.path.join(W16SETUP, "tools", "w16pack.py")
# LFNTSR (ceisserer/lnf8086): long file names for DOS, built with NASM
LFNTSR = os.path.join(ROOT, "external", "lfntsr")
DEFAULT_THEME = "WIN95"


def rt_programs():
    """Retheme16's programs; THEMES.EXE (theme switcher) where it has one."""
    progs = ["RETHEME.EXE", "RETHEME.DLL", "GALLERY.EXE"]
    if os.path.isdir(os.path.join(RETHEME, "switcher")):
        progs.append("THEMES.EXE")
    return progs


def run(cmd, cwd=None, env=None):
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("latin-1")


def watcom_env():
    watcom = os.environ.get("WATCOM", "/opt/watcom")
    env = dict(os.environ, WATCOM=watcom)
    env["PATH"] = os.pathsep.join([os.path.join(watcom, "binl64"), os.path.join(watcom, "binl"),
                                   env.get("PATH", "")])
    env["INCLUDE"] = os.pathsep.join([os.path.join(watcom, "h"), os.path.join(watcom, "h", "win")])
    return env


def build(build_dir):
    """Builds everything; returns the directories of the products."""
    for sub in (W16SETUP, RETHEME, LFNTSR):
        if not os.path.exists(os.path.join(sub, "Makefile")):
            sys.exit("%s is empty: git submodule update --init" % sub)
    if not shutil.which("nasm"):
        sys.exit("nasm is needed for LFNTSR (apt install nasm / dnf install nasm)")
    env = watcom_env()
    run(["wmake", "-h"], cwd=ROOT, env=env)

    setup_dir = os.path.join(build_dir, "win16setup")
    run(["make", "-C", W16SETUP, "O=" + setup_dir, os.path.join(setup_dir, "SETUP.EXE")], env=env)

    rt_dir = os.path.join(build_dir, "retheme16")
    run(["make", "-C", RETHEME, "-f", "win16.mk", "O=" + rt_dir] +
        [os.path.join(rt_dir, f) for f in rt_programs()], env=env)
    host_dir = os.path.join(build_dir, "retheme16-host")
    run(["make", "-C", RETHEME, "B=" + host_dir, os.path.join(host_dir, "preview")])

    lfn_dir = os.path.join(build_dir, "lfntsr")
    os.makedirs(lfn_dir, exist_ok=True)
    run(["nasm", "-f", "bin", "-I", os.path.join(LFNTSR, "src") + os.sep,
         "-o", os.path.join(lfn_dir, "LFNTSR.COM"), os.path.join(LFNTSR, "src", "lfntsr.asm")])
    return setup_dir, rt_dir, host_dir, lfn_dir


def theme_order(theme):
    """[Theme] Order= of a theme: where theme lists put it (the Windows themes
    first, then the others; themes without it last)."""
    with open(os.path.join(RETHEME, "themes", theme, "THEME.INI"), encoding="latin-1") as f:
        for line in f:
            if line.strip().lower().startswith("order="):
                try:
                    return int(line.split("=", 1)[1].split(";")[0])
                except ValueError:
                    break
    return 1000


def themes():
    """The theme directories in the order of [Theme] Order=, then by name;
    the default theme first."""
    d = os.path.join(RETHEME, "themes")
    names = [t for t in os.listdir(d) if os.path.isfile(os.path.join(d, t, "THEME.INI"))]
    names.sort(key=lambda t: (theme_order(t), t))
    names.remove(DEFAULT_THEME)
    return [DEFAULT_THEME] + names


def theme_name(theme):
    with open(os.path.join(RETHEME, "themes", theme, "THEME.INI"), encoding="latin-1") as f:
        for line in f:
            if line.strip().lower().startswith("name="):
                return line.split("=", 1)[1].strip()
    return theme


def stage(version, files, rt_dir, host_dir, preview_dir, lfn_dir):
    """The installation files in files/; returns the theme names."""
    shutil.rmtree(files, ignore_errors=True)
    os.makedirs(os.path.join(files, "RETHEME", "THEMES"))
    for f in ("NEWSHELL.EXE", "NSHOOK.DLL", "NSDLG.DLL"):
        shutil.copy(os.path.join(ROOT, "build", f.lower()), os.path.join(files, f))
    for f in rt_programs():
        shutil.copy(os.path.join(rt_dir, f), os.path.join(files, "RETHEME", f))

    with open(os.path.join(ROOT, "dist", "README.TXT")) as f:
        readme = f.read().replace("@VERSION@", version)
    with open(os.path.join(files, "README.TXT"), "wb") as f:
        f.write(crlf(readme))
    with open(os.path.join(ROOT, "COPYING")) as f:
        lic = f.read()
    with open(os.path.join(files, "COPYING.TXT"), "wb") as f:
        f.write(crlf(lic))
    rt_readme = os.path.join(RETHEME, "dist", "README.TXT")
    if os.path.exists(rt_readme):
        with open(os.path.join(RETHEME, "VERSION")) as f:
            rt_version = f.read().strip()
        with open(rt_readme) as f:
            text = f.read().replace("@VERSION@", rt_version)
        with open(os.path.join(files, "RETHEME", "README.TXT"), "wb") as f:
            f.write(crlf(text))

    # LFNTSR (Setup option "Long file names in DOS"): the driver, its
    # README and, as it is GPL software, its source
    dst = os.path.join(files, "LFNTSR")
    os.makedirs(os.path.join(dst, "SRC"))
    shutil.copy(os.path.join(lfn_dir, "LFNTSR.COM"), os.path.join(dst, "LFNTSR.COM"))
    commit = subprocess.run(["git", "-C", LFNTSR, "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip() or "unknown"
    with open(os.path.join(ROOT, "dist", "LFNTSR.TXT")) as f:
        text = f.read().replace("@COMMIT@", commit)
    with open(os.path.join(dst, "README.TXT"), "wb") as f:
        f.write(crlf(text))
    sources = [os.path.join("src", f) for f in sorted(os.listdir(os.path.join(LFNTSR, "src")))]
    for rel in sources + ["Makefile", "README.md"]:
        name = os.path.basename(rel).upper()
        with open(os.path.join(LFNTSR, rel), encoding="latin-1") as fi, \
                open(os.path.join(dst, "SRC", name), "wb") as fo:
            fo.write(crlf(fi.read()))

    tl = themes()
    for t in tl:
        dst = os.path.join(files, "RETHEME", "THEMES", t)
        shutil.copytree(os.path.join(RETHEME, "themes", t), dst)
        # the previewer's window scene as Setup's preview image
        out = os.path.join(preview_dir, t)
        os.makedirs(out, exist_ok=True)
        run([os.path.join(host_dir, "preview"), os.path.join(RETHEME, "themes", t), out])
        run([sys.executable, PACK, "preview", os.path.join(out, "window.png"),
             os.path.join(dst, "PREVIEW.BMP")])
    return tl


def setup_inf(version, theme_list, dest):
    with open(os.path.join(ROOT, "dist", "SETUP.INF"), encoding="latin-1") as f:
        text = f.read()
    items = "\n".join("%d=%s" % (i + 1, t) for i, t in enumerate(theme_list))
    switcher = "THEMES.EXE" in rt_programs()
    text = text.replace("@THEMES@", items).replace("@VERSION@", version)
    text = text.replace("@SWITCHER_ITEM@\n", "5=%DIR%\\RETHEME\\THEMES.EXE,Themes\n" if switcher else "")
    text = text.replace("@SWITCHER_RUNNING@\n", "3=THEMES,Themes\n" if switcher else "")
    with open(dest, "w", encoding="latin-1") as f:
        f.write(text)


def main():
    with open(os.path.join(ROOT, "VERSION")) as f:
        default_version = f.read().strip()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=default_version)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "release"))
    ap.add_argument("--no-floppy", action="store_true")
    a = ap.parse_args()

    build_dir = os.path.join(ROOT, "build", "external")
    setup_dir, rt_dir, host_dir, lfn_dir = build(build_dir)

    work = os.path.join(build_dir, "stage")
    files = os.path.join(work, "files")
    tl = stage(a.version, files, rt_dir, host_dir, os.path.join(work, "preview"), lfn_dir)
    inf = os.path.join(work, "SETUP.INF")
    setup_inf(a.version, tl, inf)

    os.makedirs(a.out, exist_ok=True)
    cmd = [sys.executable, PACK, "pack", "--inf", inf, "--files", files,
           "--setup", os.path.join(setup_dir, "SETUP.EXE"), "--out", a.out,
           "--name", "newshell-" + a.version, "--label", "NEWSHELL", "--version", a.version]
    if a.no_floppy:
        cmd.append("--no-floppy")
    env = dict(os.environ)
    if "SOURCE_DATE_EPOCH" not in env:
        r = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%ct"], capture_output=True, text=True)
        if r.stdout.strip():
            env["SOURCE_DATE_EPOCH"] = r.stdout.strip()
    run(cmd, env=env)
    print("themes:", ", ".join("%s (%s)" % (t, theme_name(t)) for t in tl))


if __name__ == "__main__":
    main()
