# Design notes

NewShell16 recreates [Calmira](https://en.wikipedia.org/wiki/Calmira)
(originally Delphi 1) as a plain C Win16 program. Everything is a single
executable, `NEWSHELL.EXE`.

## Modules

| File | Purpose |
|------|---------|
| `main.c` | `WinMain`, hidden main window, shell detection, hook DLL loading, start-up programs, second-instance forwarding |
| `taskbar.c` | Taskbar window: Start button, task buttons, clock, task system menus |
| `startmnu.c` | Owner-drawn Start menu from `NSSTART.INI`, `$` macros |
| `progman.c` | `PROGMAN.INI` / `*.GRP` reader, `load=`/`run=`, StartUp group |
| `desktop.c` | Desktop icons (Computer, Bin, shortcuts), desktop menu |
| `fview.c` | Folder view control: views, selection, rename, clipboard, drag and drop |
| `folder.c` | Folder windows and the Computer window (frames around a folder view) |
| `explorer.c` | File System window: folder tree plus folder view |
| `fileops.c` | Copy, move, delete, rename, new folder; progress and replace dialogs |
| `bin.c` | The Bin and its window |
| `props.c` | Properties dialog for files, folders, drives and selections |
| `taskman.c` | Task Manager, cascade and tile |
| `find.c` | Find dialog |
| `icons.c` | File icon lookup and caching |
| `dialogs.c` | Run, Shut Down, About, Options |
| `settings.c` | `NEWSHELL.INI` |
| `util.c` | 3D drawing, small images, path helpers, launching |
| `nshook.c` | `NSHOOK.DLL`: system hooks (see below) |

## Windows 3.1 specifics

- **Hooks.** System-wide hooks must live in a DLL, so like Calmira's
  `WNDHOOKS.DLL` there is `NSHOOK.DLL`. It provides a shell hook (window
  created/destroyed, for an immediate taskbar update), a `WH_CALLWNDPROC`
  hook that shrinks the maximised size in `WM_GETMINMAXINFO` so windows
  stop above the taskbar, a subclass of the desktop window for the
  right-click menu, a keyboard hook for the Windows key, and the
  undocumented `KERNEL.FileCdr` callback for file system changes. Without
  the DLL NewShell16 still works; the taskbar then relies on its timer.
- **Task list.** The taskbar also polls the top-level windows (visible,
  unowned, with a caption) from a 500 ms timer to catch title changes.
  The Retheme16 engine's window (class `Retheme16`), which only exists so
  the engine can be closed, gets no task button; Task Manager lists it.
- **Minimised windows** would normally show as icons on the desktop; with
  `HideMinApps=1` their icon position is moved below the screen with
  `SetWindowPlacement` (as Calmira does) and they are reached through the
  taskbar instead.
- **Desktop colour.** As the shell, NewShell16 sets the desktop to the
  Windows 95 teal for the session while `WIN.INI` has no `[colors]`
  `Background=` (no colour chosen in Control Panel); nothing is written.
- **Desktop icons** are small popup windows forced to the bottom of the
  Z-order; their background is drawn with `USER.PaintDesktop` so the
  wallpaper shows through. Alt+F4 on them (or on the taskbar) opens Shut
  Down.
- **Icons are 32x32 only** in Windows 3.1. Small versions for task buttons,
  menus and lists are made by drawing into a memory DC and `StretchBlt`-ing.
- **Ctrl+Esc** starts `taskman.exe=` from `SYSTEM.INI`. If that points at
  `NEWSHELL.EXE`, the second instance forwards the request to the running
  copy, which opens the Task Manager (or the Start menu).
- **The Bin** uses a hidden `NSBIN` folder on the root of each fixed
  drive, so sending something to the Bin is a rename. `NSBIN\NSBIN.INI`
  records where each item came from.
- As the shell, NewShell16 performs Program Manager's start-up duties: the
  `WIN.INI` `load=`/`run=` lines and the StartUp group (skipped while Shift
  is held).

## Look and feel, with and without a theme engine

NewShell16 draws its own parts (taskbar, Start menu, folder views, status
bars, pane edges) in the Windows 95 style, and leaves menus, captions and
dialog controls to Windows. It is meant to look consistent both on a
plain Windows 3.1 and with the
[Retheme16](https://github.com/ceisserer/win31_theme_engine) engine, which
restyles those system parts:

- **Menus.** The owner-drawn Start menu uses the font the system menus
  use: the theme's `Menu` font while the engine runs (`RETHEME.DLL`
  loaded; the font is read from the theme's `THEME.INI`, following
  `Base=`), otherwise the bold System font of Windows 3.1. Separators are
  etched with the engine and plain lines without it. Items are painted
  into a memory bitmap and blitted, so the engine, which redraws text and
  fills on menu DCs, cannot reinterpret them. Item heights follow the
  font.
- **Context menus** are owned by top-level windows (the engine only
  restyles menus of top-level windows), so they match the menu bars.
- **Controls** are only placed in dialogs (the engine themes controls in
  dialogs only): the Task Manager is a dialog, like Run, Find, Properties,
  Options and Shut Down. Lists in ordinary windows (File System tree, Bin)
  are borderless and sit in a sunken Win95 pane edge drawn by NewShell16,
  instead of the black 3.1 border.
- **Taskbar menus** are moved above the taskbar once they are on screen
  (Windows 3.1 has no `TPM_BOTTOMALIGN`).
- **Icons** of windows without a class icon (dialogs) come from
  `WM_QUERYDRAGICON`.
- **Desktop icon labels** use the icon title font from the Control Panel
  (`SPI_GETICONTITLELOGFONT`).

## Command line

| | |
|---|---|
| `NEWSHELL` | (when running) Task Manager, or Start menu with `CtrlEscStart=1` |
| `NEWSHELL /S` | Start menu |
| `NEWSHELL /E [path]` | File System window |
| `NEWSHELL path` | open a folder, document or program |

## NSSTART.INI

The Start menu uses Calmira's `START.INI` format, so existing Calmira menus
can be copied over:

```ini
[Start]
&Programs*=;;0;<programs>;0       ; '*' = submenu, items in [Start\&Programs]
&Run...=$Run;;0;<run>;0
-1=;;0;;0                         ; separator
Sh&ut Down...=$ShutDown;;0;<shutdown>;0

[Start\&Programs\Accessories]
Notepad=notepad.exe;;0;;0         ; command;directory;show;iconfile;iconindex
```

`show` is 0 (normal), 1 (minimised) or 2 (maximised). `iconfile` can be an
executable or icon file, or a built-in image: `<programs>`, `<documents>`,
`<settings>`, `<find>`, `<run>`, `<shutdown>`, `<system>`, `<folder>`,
`<help>`, `<printer>`, `<control>`, `<explorer>`, `<bin>`, `<taskman>`...

Built-in commands: `$Run`, `$ShutDown`, `$Find`, `$Explore [path]`,
`$System` (Computer), `$Bin`, `$TaskManager`, `$Folder path`,
`$MinimizePrograms`, `$ArrangeIcons`, `$TaskbarProp` (Options), `$Help`.
They can also be used for desktop shortcuts.

## NEWSHELL.INI

Most options are set in the Options dialog (Start → Settings → Taskbar).

```ini
[Taskbar]
Clock=1            ; show the clock
HideMinApps=1      ; move minimised windows off screen
MaxClamp=1         ; keep maximised windows above the taskbar
CtrlEscStart=0     ; Ctrl+Esc: 0 = Task Manager, 1 = Start menu
WinKey=1           ; Windows key opens the Start menu

[Desktop]
Icons=1
Menu=1             ; right-click menu on the desktop
Shortcut1=Notepad|NOTEPAD.EXE
Shortcut2=Documents|$Folder C:\DOCS

[Folders]
NewWindow=1        ; open sub-folders in new windows
ShowHidden=0
DeleteToBin=1
ConfirmDelete=1
FileMonitor=1      ; refresh folders when files change
View=0             ; 0 large icons, 1 small icons, 2 details, 3 list
Columns=140,70,80,72,64,64,180  ; details: Name, Size, Type, Date, Time,
                   ; Attributes, Description (dragged in the header)
Sort=0             ; 0 name, 1 type, 2 size, 3 date

[Explorer]
Split=170          ; width of the File System window's folder tree
Toolbar=1          ; View > Toolbar
StatusBar=1        ; View > Status Bar

[Startup]
Run=1              ; run load=/run= and the StartUp group when the shell
```

## Roadmap

Calmira features still to come:

- Shortcuts (aliases) as files, dragging items onto the desktop
- Documents menu, editing the Start menu from a dialog
- Rubber-band selection, dragging with the right mouse button
- Disk tools (format, label), network drive connections
- Folder window toolbar, file filters, Open With
- Screen saver / "show desktop" helpers, tips of the day
