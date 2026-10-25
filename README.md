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
- **Folder windows** and the **Computer** window: large icon, small icon
  and details views, sorting, multiple selection, type-ahead, inline
  rename, new folder, cut/copy/paste, delete (to the Bin), properties,
  drag and drop between windows, onto folders and onto the Bin, and drops
  from File Manager.
- **Explorer**: folder tree beside a folder view.
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
roadmap.

## Building

Install [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2),
then:

```sh
export WATCOM=/opt/watcom PATH=/opt/watcom/binl64:$PATH
wmake
```

Details in [docs/building.md](docs/building.md).

## Installing

1. Copy `build/newshell.exe` and `build/nshook.dll` to a directory, e.g.
   `C:\NEWSHELL\`.
2. In `SYSTEM.INI`, `[boot]` section, set
   `shell=C:\NEWSHELL\NEWSHELL.EXE`.
3. Optionally also set `taskman.exe=C:\NEWSHELL\NEWSHELL.EXE` so Ctrl+Esc
   opens NewShell's Task Manager (or the Start menu, see Options).

NewShell16 can also be started from Program Manager to try it out; then
*Shut Down → Close NewShell only* closes it again.

## Testing

[docs/testing.md](docs/testing.md) describes the DOSBox-X harness used to
boot Windows 3.1 with NewShell16 and take screenshots.

## Layout

```
src/        C sources and headers
res/        resource script, icons, bitmaps
tools/      icon generator, emulator test harness
docs/       documentation
```

## License

NewShell16 is free software, licensed under the
[GNU General Public License, version 3 or later](COPYING).

It is a re-implementation of Calmira (Copyright © Li-Hsin Huang and the
Calmira contributors), which is licensed under the GNU GPL version 2 or (at
your option) any later version. Calmira's sources were used as a reference
for behaviour and file formats.
