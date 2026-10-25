#!/usr/bin/env python3
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run NewShell16 inside Windows 3.1 under DOSBox-X for testing.

You need an installed Windows 3.1 directory (C:\\WINDOWS from an existing
installation); it is copied into a scratch "C:" drive on first use and is
never modified in place.

    tools/emu/winemu.py --windir /path/to/WINDOWS --work /tmp/nsemu \\
        --script "sleep:20 shot:boot.png key:ctrl+Escape sleep:2 shot:start.png"

Script actions (space separated):
    sleep:N         wait N seconds
    shot:FILE       screenshot of the emulator window (PNG, into --out)
    key:KEYS        xdotool key names, e.g. ctrl+Escape, Return, alt+F4
    type:TEXT       type text (use _ for spaces)
    move:DX,DY      relative mouse motion (needs the mouse to be captured)
    click:N         mouse button N (1 left, 3 right); dclick:N double-click

Requires: dosbox-x, xdotool, ImageMagick (import), and Xvfb when no
DISPLAY is available.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def prepare_drive(windir, work, exe, shell):
    cdrive = os.path.join(work, "c")
    dst_win = os.path.join(cdrive, "WINDOWS")
    if not os.path.isdir(dst_win):
        shutil.copytree(windir, dst_win)
    os.makedirs(os.path.join(cdrive, "NEWSHELL"), exist_ok=True)
    shutil.copy(exe, os.path.join(cdrive, "NEWSHELL", "NEWSHELL.EXE"))

    # SYSTEM.INI: choose the shell
    sysini = None
    for name in os.listdir(dst_win):
        if name.upper() == "SYSTEM.INI":
            sysini = os.path.join(dst_win, name)
    if sysini is None:
        sys.exit("SYSTEM.INI not found in " + dst_win)
    with open(sysini, "rb") as f:
        text = f.read().decode("latin-1")
    shell_exe = "C:\\NEWSHELL\\NEWSHELL.EXE" if shell else "progman.exe"
    text = re.sub(r"(?im)^shell\s*=.*$", lambda m: "shell=" + shell_exe, text, count=1)
    with open(sysini, "wb") as f:
        f.write(text.encode("latin-1"))
    return cdrive


def ini_set(windir, spec):
    """spec = FILE:SECTION:KEY=VALUE (KEY= with empty VALUE removes the key)"""
    fname, section, rest = spec.split(":", 2)
    key, _, value = rest.partition("=")
    path = None
    for name in os.listdir(windir):
        if name.upper() == fname.upper():
            path = os.path.join(windir, name)
    if path is None:
        path = os.path.join(windir, fname.upper())
        open(path, "wb").close()
    with open(path, "rb") as f:
        lines = f.read().decode("latin-1").splitlines()
    out, in_sec, done, found_sec = [], False, False, False
    for line in lines:
        st = line.strip()
        if st.startswith("["):
            if in_sec and not done and value:
                out.append(f"{key}={value}")
                done = True
            in_sec = st[1:-1].lower() == section.lower()
            found_sec = found_sec or in_sec
        elif in_sec and st.split("=", 1)[0].strip().lower() == key.lower():
            if not done and value:
                out.append(f"{key}={value}")
            done = True
            continue
        out.append(line)
    if not done and value:
        if not found_sec:
            out.append(f"[{section}]")
        elif in_sec is False:
            # section exists but was not last: insert after its header
            idx = next(i for i, l in enumerate(out) if l.strip().lower() == f"[{section.lower()}]")
            out.insert(idx + 1, f"{key}={value}")
            done = True
        if not done:
            out.append(f"{key}={value}")
    with open(path, "wb") as f:
        f.write(("\r\n".join(out) + "\r\n").encode("latin-1"))


def write_conf(work, cdrive):
    conf = os.path.join(work, "dosbox.conf")
    with open(conf, "w") as f:
        f.write(f"""[sdl]
output=surface
autolock=true
[dosbox]
machine=svga_s3
memsize=16
[cpu]
core=normal
cputype=486
cycles=max
[mixer]
nosound=true
[autoexec]
mount c "{cdrive}"
c:
set PATH=C:\\WINDOWS
cd \\WINDOWS
win
""")
    return conf


def find_window(display):
    env = dict(os.environ, DISPLAY=display)
    for _ in range(50):
        r = subprocess.run(["xdotool", "search", "--class", "dosbox"],
                           capture_output=True, text=True, env=env)
        ids = r.stdout.split()
        if ids:
            return ids[-1]
        time.sleep(0.2)
    return None


def run_script(script, display, out, win):
    env = dict(os.environ, DISPLAY=display)

    def xdo(*args):
        subprocess.run(["xdotool", *args], env=env, check=False)

    for action in script.split():
        kind, _, arg = action.partition(":")
        if kind == "sleep":
            time.sleep(float(arg))
        elif kind == "shot":
            path = os.path.join(out, arg)
            target = win if win else "root"
            subprocess.run(["import", "-window", target, path], env=env, check=False)
            print("screenshot:", path)
        elif kind == "key":
            xdo("key", "--window", win, "--delay", "80", *arg.split(","))
        elif kind == "type":
            xdo("type", "--window", win, "--delay", "60", arg.replace("_", " "))
        elif kind == "move":
            dx, dy = arg.split(",")
            xdo("mousemove_relative", "--", dx, dy)
        elif kind == "click":
            xdo("click", arg)
        elif kind == "dclick":
            xdo("click", "--repeat", "2", "--delay", "120", arg)
        else:
            print("unknown action:", action, file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--windir", required=True, help="installed Windows 3.1 directory")
    ap.add_argument("--work", default="/tmp/nsemu", help="scratch directory for drive C:")
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "newshell.exe"))
    ap.add_argument("--progman", action="store_true",
                    help="keep Program Manager as the shell")
    ap.add_argument("--script", default="sleep:25 shot:screen.png")
    ap.add_argument("--set", action="append", default=[], metavar="FILE:SECTION:KEY=VALUE",
                    help="set an INI value in the Windows directory before booting")
    ap.add_argument("--out", default=".", help="directory for screenshots")
    args = ap.parse_args()

    os.makedirs(args.work, exist_ok=True)
    os.makedirs(args.out, exist_ok=True)
    cdrive = prepare_drive(args.windir, args.work, args.exe, not args.progman)
    for spec in args.set:
        ini_set(os.path.join(cdrive, "WINDOWS"), spec)
    conf = write_conf(args.work, cdrive)

    xvfb = None
    display = os.environ.get("DISPLAY")
    if not display:
        display = ":77"
        xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1024x768x24"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)

    env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER="dummy")
    log = open(os.path.join(args.work, "dosbox.log"), "w")
    dosbox = subprocess.Popen(["dosbox-x", "-conf", conf, "-nopromptfolder", "-fastlaunch"],
                              env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        win = find_window(display)
        run_script(args.script, display, args.out, win)
    finally:
        dosbox.terminate()
        try:
            dosbox.wait(5)
        except subprocess.TimeoutExpired:
            dosbox.kill()
        if xvfb:
            xvfb.terminate()


if __name__ == "__main__":
    main()
