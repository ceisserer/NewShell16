#!/bin/sh
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts a Windows 3.1 image made by mkimage.sh in QEMU, in a window.
#
#   run.sh [-t] [-f FLOPPY] [-m MB] [IMAGE [QEMU-OPTION...]]
#
#   IMAGE       the disk image (default win31.img next to this script)
#   -t          try out: changes are thrown away when QEMU ends (-snapshot)
#   -f FLOPPY   a 1.44 MB disk image as drive A: (e.g. a release's
#               newshell-X.Y.Z-disk.img)
#   -m MB       memory (default 32)
#
# Anything after IMAGE goes to QEMU as it is (e.g. -vnc :1).
#
# DOS boots and starts Windows (AUTOEXEC.BAT runs WIN). Click into the
# window to give QEMU the mouse; Ctrl+Alt+G releases it. Shut Windows down
# before closing the window, or use -t.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
IMG=$HERE/win31.img
MEM=32
SNAP=
FLOPPY=

while getopts "tf:m:" opt; do
    case $opt in
    t) SNAP=-snapshot ;;
    f) FLOPPY=$OPTARG ;;
    m) MEM=$OPTARG ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
if [ $# -gt 0 ]; then
    IMG=$1
    shift
fi
EXTRA="$*"
[ -f "$IMG" ] || { echo "no image: $IMG (make one with mkimage.sh)" >&2; exit 1; }

set -- -m "$MEM" -cpu pentium -vga std -rtc base=localtime \
       -drive "file=$IMG,if=ide,format=raw" -name "Windows 3.1"
[ -n "$SNAP" ] && set -- "$@" $SNAP
[ -n "$FLOPPY" ] && set -- "$@" -drive "file=$FLOPPY,if=floppy,format=raw"
# shellcheck disable=SC2086
set -- "$@" $EXTRA
exec qemu-system-i386 "$@"
