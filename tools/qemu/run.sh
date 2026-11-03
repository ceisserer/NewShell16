#!/bin/sh
# NewShell16 - Copyright (C) 2026 NewShell16 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts a Windows 3.1 image made by mkimage.sh in QEMU, in a window.
#
#   run.sh [-t] [-n] [-d DISPLAY] [-f FLOPPY] [-m MB] [IMAGE [QEMU-OPTION...]]
#
#   IMAGE       the disk image (default win31.img next to this script)
#   -t          try out: changes are thrown away when QEMU ends (-snapshot)
#   -f FLOPPY   a 1.44 MB disk image as drive A: (e.g. a release's
#               newshell-X.Y.Z-disk.img)
#   -m MB       memory (default 32)
#   -d DISPLAY  QEMU's own window: gtk (the default) or sdl
#   -n          no KVM: emulate the CPU (slow) even if /dev/kvm is usable
#
# Anything after IMAGE goes to QEMU as it is (e.g. -vnc :1 -display none).
# QEMU's own window is faster than VNC and keeps the mouse in step; with
# VNC the PS/2 mouse is relative, so Windows' pointer can be offset from
# the client's (see README.md).
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
DISPLAY_UI=gtk
KVM=1

while getopts "tnd:f:m:" opt; do
    case $opt in
    t) SNAP=-snapshot ;;
    n) KVM= ;;
    d) DISPLAY_UI=$OPTARG ;;
    f) FLOPPY=$OPTARG ;;
    m) MEM=$OPTARG ;;
    *) sed -n '2,27p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
if [ $# -gt 0 ]; then
    IMG=$1
    shift
fi
EXTRA="$*"
[ -f "$IMG" ] || { echo "no image: $IMG (make one with mkimage.sh)" >&2; exit 1; }

# KVM runs Windows at host speed; without it QEMU emulates every instruction
if [ -n "$KVM" ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    set -- -accel kvm -cpu host
else
    [ -n "$KVM" ] && echo "no access to /dev/kvm: emulating the CPU (slow; add yourself to the kvm group)" >&2
    set -- -accel tcg -cpu pentium
fi
set -- "$@" -m "$MEM" -vga std -rtc base=localtime \
       -drive "file=$IMG,if=ide,format=raw" -name "Windows 3.1" \
       -display "$DISPLAY_UI"
[ -n "$SNAP" ] && set -- "$@" $SNAP
[ -n "$FLOPPY" ] && set -- "$@" -drive "file=$FLOPPY,if=floppy,format=raw"
# shellcheck disable=SC2086
set -- "$@" $EXTRA
exec qemu-system-i386 "$@"
