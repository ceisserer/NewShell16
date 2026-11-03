#!/bin/sh
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes a QEMU hard disk image with MS-DOS and Windows 3.1, and the setup
# files of NewShell16 (with Retheme16 bundled) and of Retheme16 copied
# onto it, not installed:
#
#   C:\NSSETUP\SETUP.EXE     NewShell16 Setup (offers the Retheme16 look)
#   C:\RTSETUP\SETUP.EXE     Retheme16 Setup on its own
#
# Boot it with run.sh and run one of them from Program Manager (File > Run).
#
#   mkimage.sh [-o IMAGE] [-d 4|8|16] [-s WxH] [-b] [-r RETHEME16]
#
#   -o IMAGE      the image to write (default win31.img next to this script)
#   -d DEPTH      colour depth: 4 = VGA 16 colours (default), 8 = 256 colours,
#                 16 = 65536 colours (the Windows XP look wants this); 8 and 16
#                 use the VBE SVGA driver vbesvga.drv
#   -s WxH        resolution for -d 8/16 (default 800x600)
#   -b            build the setups from source instead (Open Watcom etc.)
#   -r DIR        with -b: the Retheme16 checkout to build and bundle
#                 (default: the submodule external/retheme16)
#
# Everything is expected next to this script (or in $MEDIA); git ignores
# these files there:
#
#   Microsoft Windows 3.1 (3.10.103) (3.5).7z    Windows 3.1 setup disks
#   Microsoft MS-DOS 6.20 [German] (3.5).7z      MS-DOS setup disks
#   Win_31.zip                                   only for its HIMEM.SYS
#   newshell-X.Y.Z.zip                           NewShell16 release (from
#                                                GitHub Releases or a CI build)
#   retheme16-X.Y.Z.zip                          Retheme16 release (optional:
#                                                NewShell16 bundles it)
#   vbesvga-release.zip                          (-d 8/16; downloaded from
#                                                GitHub if it is missing)
#
# A release may also be its floppy image (newshell-X.Y.Z-disk.img) or an
# unpacked directory (newshell-X.Y.Z/); the highest version is used.
#
# Needs: qemu-system-i386, 7z (p7zip / p7zip-full), mtools, sfdisk
# (util-linux / fdisk), unzip, curl, git and Python 3. -b also needs gcc,
# make, Python's Pillow and Open Watcom 2.0 (looked for in $WATCOM,
# /opt/watcom and ~/.cache/newshell16/watcom, else downloaded into the last
# one). Installing Windows takes a minute or two, unattended.
#
#   Fedora:  sudo dnf install qemu-system-x86 p7zip p7zip-plugins mtools \
#                util-linux unzip curl git python3
#   Debian:  sudo apt install qemu-system-x86 p7zip-full mtools fdisk unzip \
#                curl git python3
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
PATH="$PATH:/usr/sbin:/sbin"     # sfdisk
ROOT=$(cd "$HERE/../.." && pwd)
MEDIA=${MEDIA:-$HERE}
OUT=$HERE/win31.img
DEPTH=4
RES=800x600
RT=$ROOT/external/retheme16
BUILD=0
VBE_URL=https://github.com/PluMGMK/vbesvga.drv/releases/download/v1.0-beta4/vbesvga-release.zip

while getopts "o:d:s:br:" opt; do
    case $opt in
    o) OUT=$OPTARG ;;
    d) DEPTH=$OPTARG ;;
    s) RES=$OPTARG ;;
    b) BUILD=1 ;;
    r) RT=$(cd "$OPTARG" && pwd) ;;
    *) sed -n '2,48p' "$0"; exit 2 ;;
    esac
done
case $DEPTH in 4|8|16) ;; *) echo "-d: 4, 8 or 16" >&2; exit 2 ;; esac

WIN7Z="$MEDIA/Microsoft Windows 3.1 (3.10.103) (3.5).7z"
DOS7Z="$MEDIA/Microsoft MS-DOS 6.20 [German] (3.5).7z"
WINZIP="$MEDIA/Win_31.zip"
for f in "$WIN7Z" "$DOS7Z" "$WINZIP"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
missing=
for t in qemu-system-i386 7z mcopy sfdisk unzip curl git python3; do
    command -v $t >/dev/null || missing="$missing $t"
done
[ -z "$missing" ] || { echo "missing tools:$missing (see the top of $0)" >&2; exit 1; }
# the emulator tools come from Retheme16; the build also needs Win16Setup
if [ ! -f "$ROOT/external/retheme16/Makefile" ]; then
    echo "== fetching the Retheme16 submodule (emulator tools)"
    git -C "$ROOT" submodule update --init external/retheme16
fi
if [ $BUILD = 1 ] && [ ! -f "$ROOT/external/win16setup/Makefile" ]; then
    echo "== fetching the Win16Setup submodule"
    git -C "$ROOT" submodule update --init external/win16setup
fi
EMU=$RT/tools/emu
[ -f "$EMU/mkwin31.py" ] || { echo "$RT has no tools/emu (git submodule update --init?)" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# ---- Open Watcom: $WATCOM, /opt/watcom, the cache, or download it there
watcom_bin() {
    case $(uname -m) in
    x86_64) echo "$1/binl64" ;;
    aarch64|arm64) echo "$1/arml64" ;;
    *) echo "$1/binl" ;;
    esac
}
find_watcom() {
    for d in ${WATCOM:-} /opt/watcom "$HOME/.cache/newshell16/watcom"; do
        if [ -x "$(watcom_bin "$d")/wmake" ]; then
            echo "$d"
            return
        fi
    done
}
if [ $BUILD = 1 ]; then
    missing=
    for t in gcc make; do
        command -v $t >/dev/null || missing="$missing $t"
    done
    python3 -c "import PIL" 2>/dev/null || missing="$missing python3-pillow"
    [ -z "$missing" ] || { echo "missing tools for -b:$missing" >&2; exit 1; }
    W=$(find_watcom)
    if [ -z "$W" ]; then
        W=$HOME/.cache/newshell16/watcom
        echo "== Open Watcom not found: downloading the snapshot to $W"
        mkdir -p "$W"
        curl -sSfL https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz \
            | tar xJ -C "$W"
    fi
    export WATCOM="$W"
    export PATH="$(watcom_bin "$W"):$PATH"
fi

# ---- the setup files: built, or the newest release package in $MEDIA

# newest <prefix>-X.Y.Z.zip, -X.Y.Z-disk.img or -X.Y.Z/ in a directory
find_release() {
    for f in "$1/$2"-*; do
        [ -e "$f" ] || continue
        case $f in
        *.zip|*-disk.img) ;;
        *) [ -d "$f" ] || continue ;;
        esac
        printf '%s\n' "$f"
    done | sort -V | tail -1
}

# unpacks a release into a new directory; prints the directory with SETUP.EXE
unpack_release() {
    mkdir -p "$2"
    case $1 in
    *.zip) unzip -q "$1" -d "$2" ;;
    *-disk.img) mcopy -s -i "$1" '::*' "$2/" ;;
    *) cp -R "$1"/. "$2/" ;;
    esac
    s=$(find "$2" -maxdepth 2 -iname SETUP.EXE | head -1)
    [ -n "$s" ] || { echo "no SETUP.EXE in $1" >&2; exit 1; }
    dirname "$s"
}

if [ $BUILD = 1 ]; then
    (cd "$ROOT" && RETHEME16_DIR="$RT" python3 tools/release/mkrelease.py --no-floppy)
    make -C "$RT" release
    NS_PKG=$(find_release "$ROOT/build/release" newshell)
    RT_PKG=$(find_release "$RT/build/release" retheme16)
else
    NS_PKG=$(find_release "$MEDIA" newshell)
    RT_PKG=$(find_release "$MEDIA" retheme16)
    [ -n "$NS_PKG" ] || { echo "no NewShell16 release (newshell-X.Y.Z.zip) in $MEDIA;" \
        "download one from https://github.com/ceisserer/NewShell16/releases or build with -b" >&2; exit 1; }
fi
[ -n "$NS_PKG" ] || { echo "the build made no NewShell16 release" >&2; exit 1; }
NS_REL=$(unpack_release "$NS_PKG" "$TMP/ns")
RT_REL=
if [ -n "$RT_PKG" ]; then
    RT_REL=$(unpack_release "$RT_PKG" "$TMP/rt")
fi

# ---- MS-DOS and Windows 3.1
echo "== installing MS-DOS and Windows 3.1"
7z e -y -o"$TMP/win" "$WIN7Z" >/dev/null     # flat: no folders with spaces
7z e -y -o"$TMP/dos" "$DOS7Z" >/dev/null
unzip -q -j -o "$WINZIP" "Win/WINDOWS/HIMEM.SYS" -d "$TMP"
DISK1=$(find "$TMP/dos" -iname 144OEM1.IMG | head -1)
# the tools' output (QEMU is stopped on purpose) only shows if they fail
"$EMU/mkdosdisk.sh" "$DISK1" "$TMP/dos.img" 256 >"$TMP/log" 2>&1 \
    || { cat "$TMP/log" >&2; exit 1; }
# shellcheck disable=SC2046
python3 "$EMU/mkwin31.py" --dos "$TMP/dos.img" --himem "$TMP/HIMEM.SYS" --out "$TMP/win31.img" \
    $(find "$TMP/win" -iname 'Disk0[1-6].img' | sort) >"$TMP/log" 2>&1 \
    || { cat "$TMP/log" >&2; exit 1; }

if [ "$DEPTH" != 4 ]; then
    echo "== VBE SVGA driver: $RES, $DEPTH bit"
    VBEZIP=$MEDIA/vbesvga-release.zip
    [ -f "$VBEZIP" ] || curl -sSfL -o "$VBEZIP" "$VBE_URL"
    unzip -q -o "$VBEZIP" -d "$TMP/vbe"
    python3 "$EMU/mkvbe.py" --base "$TMP/win31.img" --driver "$TMP/vbe" --out "$TMP/vbe.img" \
        --width "${RES%x*}" --height "${RES#*x}" --depth "$DEPTH" >/dev/null
    mv "$TMP/vbe.img" "$TMP/win31.img"
fi

# ---- copy the setup files (the partition starts at sector 63)
C="$TMP/win31.img@@32256"

# Mouse acceleration off (WIN.INI [windows] MouseSpeed=0): QEMU passes the
# host pointer on as relative PS/2 moves, which Windows would otherwise
# accelerate, so its pointer drifts away from the host's (badly over VNC).
mtype -i "$C" ::/WINDOWS/WIN.INI | tr -d '\r' | awk '
    /^\[/ { if (sec == "[windows]" && !done) { print "MouseSpeed=0"; done = 1 }
            sec = tolower($0) }
    sec == "[windows]" && tolower($0) ~ /^mouse(speed|threshold1|threshold2)=/ {
        if (!done) { print "MouseSpeed=0"; done = 1 }; next }
    { print }
    END { if (!done) { if (sec != "[windows]") print "[windows]"; print "MouseSpeed=0" } }
' | sed 's/$/\r/' >"$TMP/WIN.INI"
mcopy -o -i "$C" "$TMP/WIN.INI" ::/WINDOWS/WIN.INI

echo "== copying $(basename "$NS_PKG") to C:\\NSSETUP"
mmd -i "$C" ::/NSSETUP
mcopy -s -i "$C" "$NS_REL"/* ::/NSSETUP/
if [ -n "$RT_REL" ]; then
    echo "== copying $(basename "$RT_PKG") to C:\\RTSETUP"
    mmd -i "$C" ::/RTSETUP
    mcopy -s -i "$C" "$RT_REL"/* ::/RTSETUP/
fi

cp --sparse=always "$TMP/win31.img" "$OUT"
echo "== $OUT ready; start it with: $HERE/run.sh $OUT"
