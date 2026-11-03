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
#   mkimage.sh [-o IMAGE] [-d 4|8|16] [-s WxH] [-r RETHEME16] [-n]
#
#   -o IMAGE      the image to write (default win31.img next to this script)
#   -d DEPTH      colour depth: 4 = VGA 16 colours (default), 8 = 256 colours,
#                 16 = 65536 colours (the Windows XP look wants this); 8 and 16
#                 use the VBE SVGA driver vbesvga.drv
#   -s WxH        resolution for -d 8/16 (default 800x600)
#   -r DIR        Retheme16 checkout to build and bundle (default: the
#                 submodule external/retheme16)
#   -n            do not build; use the release directories already in
#                 build/release (NewShell16) and DIR/build/release (Retheme16)
#
# The licensed files are expected next to this script (or in $MEDIA), as
# they are named on the download server; nothing of them is committed:
#
#   Microsoft Windows 3.1 (3.10.103) (3.5).7z    Windows 3.1 setup disks
#   Microsoft MS-DOS 6.20 [German] (3.5).7z      MS-DOS setup disks
#   Win_31.zip                                   only for its HIMEM.SYS
#   vbesvga-release.zip                          (-d 8/16; downloaded from
#                                                GitHub if it is missing)
#
# Needs: qemu-system-i386, 7z (p7zip / p7zip-full), mtools, sfdisk
# (util-linux / fdisk), unzip, curl, Python 3, and for the build gcc, make,
# Python's Pillow and Open Watcom 2.0. Open Watcom is looked for in $WATCOM,
# /opt/watcom and ~/.cache/newshell16/watcom; if it is in none of them, the
# current snapshot (about 100 MB) is downloaded into the last one. Installing
# Windows takes a minute or two, unattended.
#
#   Fedora:  sudo dnf install qemu-system-x86 p7zip p7zip-plugins mtools \
#                util-linux unzip curl gcc make python3-pillow
#   Debian:  sudo apt install qemu-system-x86 p7zip-full mtools fdisk unzip \
#                curl gcc make python3-pil
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
PATH="$PATH:/usr/sbin:/sbin"     # sfdisk
ROOT=$(cd "$HERE/../.." && pwd)
MEDIA=${MEDIA:-$HERE}
OUT=$HERE/win31.img
DEPTH=4
RES=800x600
RT=$ROOT/external/retheme16
BUILD=1
VBE_URL=https://github.com/PluMGMK/vbesvga.drv/releases/download/v1.0-beta4/vbesvga-release.zip

while getopts "o:d:s:r:n" opt; do
    case $opt in
    o) OUT=$OPTARG ;;
    d) DEPTH=$OPTARG ;;
    s) RES=$OPTARG ;;
    r) RT=$(cd "$OPTARG" && pwd) ;;
    n) BUILD=0 ;;
    *) sed -n '2,40p' "$0"; exit 2 ;;
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
for t in qemu-system-i386 7z mcopy sfdisk unzip curl python3; do
    command -v $t >/dev/null || missing="$missing $t"
done
if [ $BUILD = 1 ]; then
    for t in gcc make; do
        command -v $t >/dev/null || missing="$missing $t"
    done
    python3 -c "import PIL" 2>/dev/null || missing="$missing python3-pillow"
fi
[ -z "$missing" ] || { echo "missing tools:$missing (see the top of $0)" >&2; exit 1; }
if [ ! -f "$ROOT/external/retheme16/Makefile" ] || [ ! -f "$ROOT/external/win16setup/Makefile" ]; then
    echo "== fetching the submodules (Retheme16, Win16Setup)"
    git -C "$ROOT" submodule update --init
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

# ---- the setup files
if [ $BUILD = 1 ]; then
    (cd "$ROOT" && RETHEME16_DIR="$RT" python3 tools/release/mkrelease.py --no-floppy)
    make -C "$RT" release
fi
NS_REL=$(ls -d "$ROOT"/build/release/newshell-*/ 2>/dev/null | tail -1)
RT_REL=$(ls -d "$RT"/build/release/retheme16-*/ 2>/dev/null | tail -1)
[ -n "$NS_REL" ] || { echo "no NewShell16 release in $ROOT/build/release" >&2; exit 1; }
[ -n "$RT_REL" ] || { echo "no Retheme16 release in $RT/build/release" >&2; exit 1; }

# ---- MS-DOS and Windows 3.1
echo "== installing MS-DOS and Windows 3.1"
7z e -y -o"$TMP/win" "$WIN7Z" >/dev/null     # flat: no folders with spaces
7z e -y -o"$TMP/dos" "$DOS7Z" >/dev/null
unzip -q -j -o "$WINZIP" "Win/WINDOWS/HIMEM.SYS" -d "$TMP"
DISK1=$(find "$TMP/dos" -iname 144OEM1.IMG | head -1)
"$EMU/mkdosdisk.sh" "$DISK1" "$TMP/dos.img" 256 >/dev/null
# shellcheck disable=SC2046
python3 "$EMU/mkwin31.py" --dos "$TMP/dos.img" --himem "$TMP/HIMEM.SYS" --out "$TMP/win31.img" \
    $(find "$TMP/win" -iname 'Disk0[1-6].img' | sort) >/dev/null

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
echo "== copying $(basename "$NS_REL") to C:\\NSSETUP and $(basename "$RT_REL") to C:\\RTSETUP"
C="$TMP/win31.img@@32256"
mmd -i "$C" ::/NSSETUP ::/RTSETUP
mcopy -s -i "$C" "$NS_REL". ::/NSSETUP/
mcopy -s -i "$C" "$RT_REL". ::/RTSETUP/

cp --sparse=always "$TMP/win31.img" "$OUT"
echo "== $OUT ready; start it with: $HERE/run.sh $OUT"
