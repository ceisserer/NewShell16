# Testing in an emulator

`tools/emu/winemu.py` boots Windows 3.1 in [DOSBox-X](https://dosbox-x.com/)
with NewShell16 installed as the shell, drives it with keystrokes and takes
screenshots. It works headless (it starts `Xvfb` when no `DISPLAY` is set).

Requirements (Debian/Ubuntu): `dosbox-x xdotool imagemagick xvfb`.

You need an installed Windows directory, e.g. `Win/WINDOWS` from
`Win_31.zip` (see [reference-media.md](reference-media.md)). It is copied
into a scratch drive C: and the copy is modified, never the original.

```sh
wmake
tools/emu/winemu.py --windir ~/win31/Win/WINDOWS --work /tmp/nsemu --out shots \
    --set 'SYSTEM.INI:boot:taskman.exe=C:\NEWSHELL\NEWSHELL.EXE' \
    --script "sleep:22 shot:desktop.png key:ctrl+Escape sleep:2 shot:start.png"
```

- `--progman` keeps Program Manager as the shell (NewShell16 can then be
  started from it, or via `--set 'WIN.INI:windows:run=C:\NEWSHELL\NEWSHELL.EXE'`).
- `--set FILE:SECTION:KEY=VALUE` edits an INI file in the Windows
  directory before booting; an empty value removes the key. Changes persist
  in the `--work` directory.
- Script actions: `sleep:N`, `shot:FILE`, `key:KEYS` (xdotool names,
  comma separated), `type:TEXT` (`_` for spaces), `move:DX,DY`,
  `goto:X,Y`, `click:N`, `dclick:N`, `down:N`, `up:N`.

`--lfn` starts DOSBox-X's DOS as version 7.1 with long file names
(`[dos] lfn=true`), the same LFN API that DOSLFN, LFNDOS or Windows 95
give: files made in the scratch drive (`WORK/c`) from Linux keep their
long names. Without it NewShell16 runs with 8.3 names. Example: the Open
dialog of Notepad in the Windows XP look:

```sh
mkdir -p "/tmp/nsemu/c/My Documents" && echo hi > "/tmp/nsemu/c/My Documents/A long name.txt"
tools/emu/winemu.py --lfn --windir ~/win31/Win/WINDOWS --work /tmp/nsemu --out shots \
    --set 'WIN.INI:windows:run=NOTEPAD.EXE' --set 'NEWSHELL.INI:FileDialog:Style=2' \
    --script "sleep:30 key:alt+f key:o sleep:3 type:C:\\My_Documents key:Return sleep:2 shot:open.png"
```

`tools/emu/ofntest/ofntest.c` checks what the dialogs return: it calls
`GetOpenFileName` (multiple selection), `GetSaveFileName` (default
extension `TXT`) and `GetOpenFileName` with `C:\MYDOCU~1\*.WRI`, and
writes the results to `C:\OFN.TXT`:

```sh
wcc -q -bt=windows -ml -zW -2 -i=$WATCOM/h -i=$WATCOM/h/win tools/emu/ofntest/ofntest.c -fo=/tmp/ofn.obj
wlink system windows name /tmp/OFN.EXE option quiet library commdlg.lib file /tmp/ofn.obj
wrc -q -bt=windows -31 /tmp/OFN.EXE
tools/emu/winemu.py --lfn ... --install /tmp/OFN.EXE=OFN.EXE --set 'WIN.INI:windows:run=C:\OFN.EXE' \
    --script 'sleep:30 type:C:\\My_Documents key:Return sleep:2 type:"A_long_name.txt"_"Another_one.txt" key:Return ...'
```

```
OPEN r=1 file=[C:\MYDOCU~1 LETTER~1.TXT NOTESF~1.TXT] title=[] off=12 ext=0 idx=1 flags=00001204
SAVE r=1 file=[C:\MYDOCU~1\REPORT~1.TXT] title=[REPORT~1.TXT] off=12 ext=21 idx=1 flags=00000006
```

With `--set 'SYSTEM.INI:boot:display.drv=vbesvga.drv'` and the other
settings of `tools/emu/mkvbe.py` in Retheme16 (the files of
[vbesvga.drv](https://github.com/PluMGMK/vbesvga.drv) installed into
`WINDOWS/SYSTEM` with `--install`), DOSBox-X runs Windows at 800x600 with
65536 colours, for the XP and 11 looks with long names.

Setting `taskman.exe=` to NewShell16 makes Ctrl+Esc open NewShell16's Task
Manager (or, with `--set 'NEWSHELL.INI:Taskbar:CtrlEscStart=1'`, the Start
menu), which allows driving most of the shell from the keyboard.

Mouse input is relative under DOSBox. With `MouseSpeed=0` in WIN.INI
(`--set 'WIN.INI:windows:MouseSpeed=0'`) one host pixel moves the Windows
pointer one pixel, and `goto:X,Y` first pins the pointer to the top-left
corner, so clicks and drags can be scripted at screen coordinates. Start
the script with `click:1` so DOSBox captures the mouse. Screenshots include
the DOSBox-X menu bar, so Windows' y = image y - 17.

Example: drag a file onto the Bin icon

```sh
--script "sleep:20 click:1 goto:75,149 click:1 down:1 move:-10,0 move:-20,-60 up:1"
```

## Testing with the Retheme16 theme engine

Build [Retheme16](https://github.com/ceisserer/win31_theme_engine)
(`make -f win16.mk O=/tmp/rt` builds `RETHEME.EXE` and `RETHEME.DLL`), then
install it on the test drive and start it through `load=`:

```sh
tools/emu/winemu.py --windir ~/win31/Win/WINDOWS --work /tmp/nsemu-rt --out shots \
    --install /tmp/rt/RETHEME.EXE=RETHEME/RETHEME.EXE \
    --install /tmp/rt/RETHEME.DLL=RETHEME/RETHEME.DLL \
    --install ~/win31_theme_engine/themes=RETHEME/THEMES \
    --set 'WIN.INI:windows:load=C:\RETHEME\RETHEME.EXE' --script "..."
```

`--install SRC=DEST` copies a file or directory to `DEST` on drive C:.
Run the same script with and without the engine and compare the
screenshots side by side.

## Windows 3.1 in QEMU with 65536 colours (the XP look)

For trying things by hand, [tools/qemu](../tools/qemu/README.md) has two
scripts: `mkimage.sh` makes such an image with the prebuilt NewShell16 and
Retheme16 setups (release packages next to it) copied onto it, not
installed; `run.sh` starts it in a window. The steps below are what they do.

Windows 3.1 (3.10.103) is preferred over Windows for Workgroups 3.11 for
testing (`Win_31.zip` is in fact WfW 3.11). Retheme16's emulator tools
(`external/retheme16/tools/emu`, see its README) install Windows 3.1 from
the floppies into a QEMU disk image, and switch it to the generic VBE
driver [vbesvga.drv](https://github.com/PluMGMK/vbesvga.drv), which gives
16-bit colour (65536 colours) in QEMU's standard VGA:

```sh
E=external/retheme16/tools/emu
$E/mkdosdisk.sh 144OEM1.IMG dos.img 256          # MS-DOS setup disk 1
$E/mkwin31.py --dos dos.img --himem HIMEM.SYS --out win31.img Disk0[1-6].img
curl -LO https://github.com/PluMGMK/vbesvga.drv/releases/download/v1.0-beta4/vbesvga-release.zip
mkdir vbe && unzip -d vbe vbesvga-release.zip
$E/mkvbe.py --base win31.img --driver vbe --out win31hc.img --width 800 --height 600 --depth 16
```

`HIMEM.SYS` must be uncompressed (the one on the disks is compressed; an
installed `WINDOWS` directory has one). To make NewShell16 the shell of
that image, copy it in with mtools (the partition starts at sector 63) and
set `shell=`:

```sh
C=win31hc.img@@32256
mmd -i $C ::/NEWSHELL
mcopy -i $C build/newshell.exe ::/NEWSHELL/NEWSHELL.EXE
mcopy -i $C build/nshook.dll ::/NEWSHELL/NSHOOK.DLL
mtype -i $C ::/WINDOWS/SYSTEM.INI | sed 's/^shell=.*/shell=C:\\NEWSHELL\\NEWSHELL.EXE/' > sys.ini
mcopy -o -i $C sys.ini ::/WINDOWS/SYSTEM.INI
```

Retheme16's `run.py` then deploys the engine with a theme (on `load=`,
which NewShell16 runs as the shell), boots, and takes screenshots:

```sh
(cd external/retheme16 && make win16)
$E/run.py --base win31hc.img --out shots --theme WINXP --gallery styles --boot 55 \
    --step "shot desktop" --step "click 30 588" --step 2 --step "shot start"
```

## Testing the installer

`--no-install` leaves out NewShell16 (no copy, `shell=` unchanged), so the
release can be installed with its own Setup from Program Manager:

```sh
wmake release
tools/emu/winemu.py --windir ~/win31/Win/WINDOWS --work /tmp/nsemu-setup --out shots \
    --progman --no-install --install build/release/newshell-0.4.0=SETUPSRC \
    --set 'WIN.INI:windows:run=C:\SETUPSRC\SETUP.EXE' --script "sleep:20 shot:setup.png ..."
```

Keep the `--work` directory between runs to test updates, option changes
and removal on an installed system; with NewShell16 as the shell these go
through Win16Setup's shell handover (a restart with Program Manager).
