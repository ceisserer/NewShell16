# NewShell16

A re-implementation of the [Calmira](https://en.wikipedia.org/wiki/Calmira)
shell for Windows 3.1x, written in plain C against the Win16 API.

The original Calmira (by Li-Hsin Huang) was written in Delphi 1 and brought a
Windows 95-style desktop to Windows 3.1. NewShell16 recreates that
experience as a small, native 16-bit C program.

## Status

Early but usable: it runs as the Windows 3.1 shell.

- Taskbar with Start button, a button per running application (with its
  icon, click to activate/minimise, right-click for its system menu) and a
  clock
- Start menu with Program Manager groups under *Programs*, custom entries,
  Computer, Settings, Run and Shut Down
- Desktop icons (Computer plus configurable shortcuts), draggable
- Folder windows with large icons (icons taken from the programs and file
  associations) and a Computer window listing the drives
- Run, Shut Down and About dialogs
- Runs `load=`/`run=` and the StartUp group when it is the shell

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

1. Copy `build/newshell.exe` to e.g. `C:\NEWSHELL\NEWSHELL.EXE`.
2. In `SYSTEM.INI`, `[boot]` section, set
   `shell=C:\NEWSHELL\NEWSHELL.EXE`.
3. Optionally also set `taskman.exe=C:\NEWSHELL\NEWSHELL.EXE` so Ctrl+Esc
   opens the Start menu.

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
