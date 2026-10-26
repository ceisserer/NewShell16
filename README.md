# NewShell16

A re-implementation of the [Calmira](https://en.wikipedia.org/wiki/Calmira)
shell for Windows 3.1x, written in plain C against the Win16 API.

The original Calmira (by Li-Hsin Huang) was written in Delphi 1 and brought a
Windows 95-style desktop to Windows 3.1. NewShell16 recreates that
experience as a small, native 16-bit C program.

## Status

Usable as the Windows 3.1 shell. Implemented so far:

- **Taskbar** with Start button, a button per application (icon, click to
  activate/minimise, right-click for its system menu), clock, and a menu to
  cascade/tile/minimise windows. Minimised windows are kept off the
  desktop and maximised windows stop above the taskbar.
- **Start menu** in Calmira's START.INI format (`NSSTART.INI`), created
  from the Program Manager groups on first use, with icons at every level.
- **Desktop** with Computer, Bin and configurable shortcut icons, and a
  right-click menu.
- **Folder windows** and the **Computer** window: large icon, small icon,
  list and details views (a table with resizable columns, including the
  descriptions of `DESCRIPT.ION`), sorting, multiple selection, type-ahead, inline
  rename, new folder, cut/copy/paste, delete (to the Bin), properties,
  drag and drop between windows, onto folders and onto the Bin, and drops
  from File Manager.
- **File System**: folder tree beside a folder view, with a toolbar (up,
  new folder, cut/copy/paste, delete, properties, views, sort order) and a
  two-part status bar.
- **Bin**: deleted items can be restored or deleted for good.
- **Task Manager** (Ctrl+Esc): switch to, hide, minimise, end or terminate
  tasks, start new ones, cascade and tile.
- **Find** files and folders.
- Run, Shut Down, Properties, Options and About dialogs.
- Folder windows refresh by themselves when files change.
- As the shell it runs `load=`/`run=` and the StartUp group.

It looks consistent both on a plain Windows 3.1 and with the
[Retheme16](https://github.com/ceisserer/win31_theme_engine) Windows 95
theme engine.

See [docs/design.md](docs/design.md) for internals, configuration and the
roadmap, and [docs/calmira-parity.md](docs/calmira-parity.md) for what
Calmira has that NewShell16 does not have yet.

## Building

Install [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2),
then:

```sh
export WATCOM=/opt/watcom PATH=/opt/watcom/binl64:$PATH
wmake
```

Details in [docs/building.md](docs/building.md).

## Installing

Unzip a release (or `wmake release`, see
[docs/building.md](docs/building.md)) and run `SETUP.EXE` from Windows. Setup
can make NewShell16 the Windows shell and optionally installs the Retheme16
Windows 95 look with a theme of your choice; both can be changed later by
running Setup again from the NewShell16 group. `SETUP /?` lists the switches
for silent installation.

By hand:

1. Copy `build/newshell.exe` and `build/nshook.dll` to a directory, e.g.
   `C:\NEWSHELL\`.
2. In `SYSTEM.INI`, `[boot]` section, set
   `shell=C:\NEWSHELL\NEWSHELL.EXE`.
3. Optionally also set `taskman.exe=C:\NEWSHELL\NEWSHELL.EXE` so Ctrl+Esc
   opens NewShell16's Task Manager (or the Start menu, see Options).

NewShell16 can also be started from Program Manager to try it out; then
*Shut Down → Close NewShell16 only* closes it again.

### Trying it in an emulator

You need your own copy of Windows 3.1 (see
[docs/reference-media.md](docs/reference-media.md)) and the floppy image of
a [release](https://github.com/ceisserer/NewShell16/releases):

```sh
V=0.2.0
curl -LO https://github.com/ceisserer/NewShell16/releases/download/v$V/newshell-$V-disk.img
```

**DOSBox-X** (or DOSBox Staging, with `dosbox` instead of `dosbox-x`) with
the pre-installed `Win_31.zip`: unpack its `WINDOWS` directory into a
directory that becomes drive C:, then start Windows with Setup from the
floppy:

```sh
mkdir -p ~/win31 && unzip -q Win_31.zip -d ~/win31/zip
mkdir -p ~/win31/c && cp -r ~/win31/zip/Win/WINDOWS ~/win31/c/
# WIN.INI in Win_31.zip ends in NUL bytes, which hide every section added
# to it later (settings of all programs, Setup's update across a restart)
sed -i 's/\x0//g' ~/win31/c/WINDOWS/*.INI

dosbox-x -c "mount c $HOME/win31/c" -c "imgmount a $PWD/newshell-$V-disk.img -t floppy" \
         -c "c:" -c "cd \\windows" -c "win a:\\setup.exe"
```

Setup installs to `C:\NEWSHELL` and restarts Windows with NewShell16 as the
shell. Later sessions need only
`dosbox-x -c "mount c $HOME/win31/c" -c "c:" -c "cd \\windows" -c "win"`.
The installation lives in `~/win31/c`; delete it to start over.

**QEMU** needs a bootable hard disk image with DOS and Windows 3.1 (for
example installed from the Windows 3.1 and MS-DOS floppies). Attach the
NewShell16 floppy, boot, and run `win a:\setup.exe` at the DOS prompt:

```sh
qemu-system-i386 -m 16 -hda win31.img -fda newshell-$V-disk.img
```

## Testing

[docs/testing.md](docs/testing.md) describes the DOSBox-X harness used to
boot Windows 3.1 with NewShell16 and take screenshots.

## Layout

```
src/        C sources and headers
res/        resource script, icons, bitmaps
tools/      icon generator, emulator test harness, release script
dist/       installer description (SETUP.INF) and the user README
external/   Win16Setup and Retheme16 (git submodules)
docs/       documentation
```

## License

NewShell16 is free software, licensed under the
[GNU General Public License, version 3 or later](COPYING).

It is a re-implementation of Calmira (Copyright © Li-Hsin Huang and the
Calmira contributors), which is licensed under the GNU GPL version 2 or (at
your option) any later version. Calmira's sources were used as a reference
for behaviour and file formats, and most icons and small images are
Calmira's (`res/calmira/`).
