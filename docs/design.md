# Design notes

NewShell16 recreates [Calmira](https://en.wikipedia.org/wiki/Calmira)
(originally Delphi 1) as a plain C Win16 program. Everything is a single
executable, `NEWSHELL.EXE`.

## Modules

| File | Purpose |
|------|---------|
| `main.c` | `WinMain`, hidden main window, shell detection, start-up programs, second-instance forwarding |
| `taskbar.c` | Taskbar window: Start button, task buttons, clock, task system menus |
| `startmnu.c` | Owner-drawn Start menu, Programs submenu built from Program Manager groups |
| `progman.c` | `PROGMAN.INI` / `*.GRP` reader, `load=`/`run=`, StartUp group |
| `desktop.c` | Desktop icons (Computer, configurable shortcuts) |
| `folder.c` | Folder windows and the Computer (drives) window |
| `icons.c` | File icon lookup and caching |
| `dialogs.c` | Run, Shut Down, About |
| `settings.c` | `NEWSHELL.INI` |
| `util.c` | 3D drawing, small images, path helpers, launching |

## Windows 3.1 specifics

- **No shell hooks.** The task list is refreshed from a 500 ms timer by
  enumerating top-level windows (visible, unowned, with a caption).
- **Minimised windows** would normally show as icons on the desktop; with
  `HideMinApps=1` they are moved below the bottom of the screen and are
  reached through the taskbar instead (as Calmira did).
- **Desktop icons** are small popup windows forced to the bottom of the
  Z-order; their background is drawn with `USER.PaintDesktop` so the
  wallpaper shows through.
- **Icons are 32x32 only** in Windows 3.1. Small versions for task buttons
  and menus are made by drawing into a memory DC and `StretchBlt`-ing.
- **Ctrl+Esc** starts `taskman.exe=` from `SYSTEM.INI`. If that points at
  `NEWSHELL.EXE`, the second instance forwards the request to the running
  copy, which opens the Start menu.
- As the shell, NewShell performs Program Manager's start-up duties: the
  `WIN.INI` `load=`/`run=` lines and the StartUp group (skipped while Shift
  is held).

## NEWSHELL.INI

```ini
[Taskbar]
Clock=1            ; show the clock
HideMinApps=1      ; move minimised windows off screen

[Desktop]
Icons=1
Shortcut1=Notepad|NOTEPAD.EXE

[Start Menu]       ; replaces the default File Manager / MS-DOS Prompt entries
Item1=File Manager|WINFILE.EXE
Item2=Write|WRITE.EXE

[Folders]
NewWindow=1        ; open sub-folders in new windows
ShowHidden=0

[Startup]
Run=1              ; run load=/run= and the StartUp group when the shell
```

## Roadmap

Not yet implemented (Calmira features still to come):

- File operations: copy/move/delete, drag and drop between folder windows
- Bin (recycle bin)
- Shortcuts as files (drag to desktop), properties dialogs
- Find dialog, Documents menu
- Small icon / list / details views in folder windows
- Options dialogs (taskbar, desktop, start menu editor)
- Keeping maximised windows clear of the taskbar
