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
| `startprop.c` | Start Menu Properties: the Start menu as an editable tree, its look |
| `progman.c` | `PROGMAN.INI` / `*.GRP` reader, `load=`/`run=`, StartUp group |
| `dde.c` | Program Manager DDE server for setup programs |
| `desktop.c` | Desktop icons (Computer, Bin, shortcuts), desktop menu, drops on the desktop |
| `shortcut.c` | Shortcut Properties and Change Icon dialogs |
| `alias.c` | Aliases (`.ALS` files in Calmira's format) |
| `fview.c` | Folder view control: views, selection, rubber band, rename, clipboard, drag and drop, hints |
| `folddlg.c` | Select Files, Set Filter, Go To, Open With, Describe, Columns; `DESCRIPT.ION` |
| `folder.c` | Folder windows and the Computer window (frames around a folder view) |
| `explorer.c` | File System and Browse Folders windows: folder tree plus folder view |
| `toolbar.c` | Toolbar of folder and File System windows |
| `fileops.c` | Copy, move, delete, rename, new folder; progress, replace and confirmation dialogs |
| `bin.c` | The Bin, its window and its properties |
| `props.c` | Properties dialog: files, folders, drives (pie), selections, version, association |
| `taskman.c` | Task Manager, cascade and tile |
| `find.c` | Find dialog: names, contents, result menu |
| `icons.c` | File icon lookup and caching |
| `dialogs.c` | Run, Shut Down, About |
| `options.c` | The Options dialog |
| `system.c` | Sounds, web addresses, splash and shutdown screens, Tip of the Day, help, saved windows |
| `users.c` | Users: logon dialog, per-user settings |
| `settings.c` | `NEWSHELL.INI` |
| `util.c` | 3D drawing, small images, colours, hints, path helpers, launching |
| `nshook.c` | `NSHOOK.DLL`: system hooks (see below) |

## Windows 3.1 specifics

- **Hooks.** System-wide hooks must live in a DLL, so like Calmira's
  `WNDHOOKS.DLL` there is `NSHOOK.DLL`. It provides a shell hook (window
  created/destroyed, for an immediate taskbar update), a `WH_CALLWNDPROC`
  hook that shrinks the maximised size in `WM_GETMINMAXINFO` so windows
  stop above the taskbar, a subclass of the desktop window for the
  right-click menu and for files dropped from File Manager, a keyboard
  hook for the Windows key and hot keys, a `WH_GETMESSAGE` hook that
  closes windows on a right click on their minimise or maximise box,
  and the undocumented `KERNEL.FileCdr` callback for file system
  changes. Without the DLL NewShell16 still works; the taskbar then
  relies on its timer.
- **Program Manager DDE.** Setup programs create their groups by DDE
  with Program Manager (service and topic `PROGMAN`). As the shell,
  NewShell16 answers in its place with plain DDE messages (which DDEML
  clients understand), one hidden window per conversation, and turns
  the commands into submenus of Programs in `NSSTART.INI`.
- **One data segment.** Windows 3.1 runs a program with several data
  segments only once, and Ctrl+Esc relies on a second copy of
  NEWSHELL.EXE handing its command line to the first. String literals
  therefore go into the code segments (`-zc`) and large tables are
  allocated from the global heap.
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
  `Base=`), otherwise the bold System font of Windows 3.1. As in
  Calmira's 3D Start menu, items use the button-face colour, the first
  level has 32x32 icons and a banner (a gradient towards the active
  caption colour), and separators are etched. Without the engine the
  items also paint a raised Windows 95 frame: its inner lines are part of
  the items, and its outer lines are drawn over the black menu border
  Windows 3.1 draws (the owner-draw DC is not clipped to the item). Items
  are painted into a memory bitmap and blitted, so the engine, which
  redraws text and fills on menu DCs, cannot reinterpret them. Item
  heights follow the font.
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
| `NEWSHELL /D` | show the desktop (minimise everything; again: restore) |
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
`<help>`, `<printer>`, `<control>`, `<explorer>`, `<bin>`, `<taskman>`,
`<favorites>`, `<internet>`...

Built-in commands: `$Run`, `$ShutDown`, `$Find`, `$Explore [path]`,
`$BrowseFolders`, `$System` (Computer), `$Bin`, `$TaskManager`,
`$Folder path`, `$MinimizePrograms`, `$ClearDesktop`, `$CascadeBrowsers`,
`$CloseBrowsers`, `$ArrangeIcons`, `$LineUpIcons`, `$ScreenSaver`,
`$ToggleTaskbar`, `$Tips`, `$Help`, `$TaskbarProp`, `$DesktopProp`,
`$FileSystemProp`, `$SystemProp` (pages of the Options), `$StartMenuProp`,
`$BinProp`. They can also be used for desktop shortcuts, aliases and hot
keys. Commands starting with `http://`, `ftp://`, `www.` and the like
open in the web browser.

Settings > Start Menu (`$StartMenuProp`) edits the menu as a tree and
writes `NSSTART.INI` anew. Setup programs add their groups to Programs
over DDE.

## NEWSHELL.INI

Nearly every option is in the Options dialog (Start > Settings >
Taskbar); the look of the Start menu is in Start Menu Properties.

```ini
[Taskbar]
Clock=1            ; show the clock
HideMinApps=1      ; move minimised windows off screen
MinAbove=0         ; (HideMinApps=0) icons of minimised windows above the bar
MaxClamp=1         ; keep maximised windows above the taskbar
CtrlEscStart=0     ; Ctrl+Esc: 0 = Task Manager, 1 = Start menu
WinKey=1           ; Windows key: Start menu, Win+E/R/F/M/D/Tab
AutoHide=0         ; slide away, back when the mouse reaches the bottom
AnimSteps=6
Enabled=1          ; 0: no taskbar
Hints=1            ; captions, date and resources as hints
DocFirst=0         ; "README.TXT - Notepad"
LowerDoc=0         ; ... "readme.txt - Notepad"
Exclude=           ; programs (module names) without a button
Tray=              ; programs shown as icons beside the clock

[Desktop]
Icons=1
Menu=1             ; right-click menu on the desktop
SnapToGrid=1
SingleClick=0
ShortcutArrows=1
RightClose=0       ; right click on minimise/maximise boxes closes
ComputerCaption=Computer
BinCaption=Bin
Shortcut1=Notepad|NOTEPAD.EXE
Shortcut2=Documents|$Folder C:\DOCS
Shortcut3=Write|WRITE.EXE|C:\DOCS|2|PROGMAN.EXE|5
                   ; caption|command|folder|show (0 1 2)|icon file|icon index

[Folders]
NewWindow=1        ; open sub-folders in new windows
ShowHidden=0
Toolbar=1
StatusBar=1
FlatToolbar=0
FileMonitor=1      ; refresh folders when files change
View=0             ; 0 large icons, 1 small icons, 2 details, 3 list
Columns=140,70,80,72,64,64,180  ; details: Name, Size, Type, Date, Time,
                   ; Attributes, Description (dragged in the header)
HiddenColumns=0    ; bit n switches column n off (View > Columns)
Sort=0             ; 0 name, 1 type, 2 size, 3 date
Capitalize=0       ; "Calc.exe"
HideExtensions=0   ; no .EXE for programs with their own icon
ShadeSelected=1
FileHints=1
WindowPos=0        ; new windows: 0 Windows, 1 cascaded, 2 like the last one
SaveWindows=0      ; reopen the windows of the last session
Inspect=NOTEPAD.EXE
DeleteToBin=1
ConfirmDelete=1
ConfirmCopy=0      ; before copying or moving
ConfirmReplace=1   ; each replaced file
ConfirmFolders=1   ; copying into an existing folder
ConfirmProtected=1 ; read-only, hidden and system files
AskOnDrop=0        ; Move / Copy / Alias menu after every drag
DragCopy=0         ; drags copy unless Shift is held

[Explorer]
Split=170          ; width of the File System window's folder tree
Toolbar=1          ; View > Toolbar
StatusBar=1        ; View > Status Bar
RememberLast=1     ; open at the last folder (Last=)

[Start Menu]
Win95Look=1        ; banner and 3D frame, else a plain menu
ColouredBar=1
BoldSelect=0
LargeTop=1         ; large icons on the first level
ItemHeight=34
Font=              ; "Arial,9,700,0": face, points, weight, italic
MouseUp=0          ; open when the Start button is released
ProgramsGroup=     ; the submenu for Program Manager groups (default Programs)

[Startup]
Run=1              ; run load=/run= and the StartUp group when the shell
ProgmanDDE=1       ; answer setup programs in Program Manager's place

[Preferences]
Splash=0
ShutdownScreen=1
ShowTips=0
OpenComputer=0     ; open the Computer window at start-up
ComputerQuits=0    ; closing the Computer window shuts down

[Bin]
Capacity=0         ; MB, 0 = no limit (the oldest items go first)
EmptyOnExit=0

[Colors]           ; "r g b"; the system colours when not set
Taskbar=
StartMenu=
Selection=

[Sounds]           ; .WAV files
Startup=
Exit=
StartMenu=
OpenFolder=
EmptyBin=

[Internet]
Browser=           ; else the program associated with .HTM

[Hotkeys]
1=Ctrl+Alt+N,NOTEPAD.EXE   ; key, command (or $ macro)

[Find]
Icons=1
MatchCase=0

[Users]            ; always in the shared NEWSHELL.INI
Enabled=0          ; logon dialog; user n has NSUSERn.INI and NSMENUn.INI
```

## Roadmap

[calmira-parity.md](calmira-parity.md) compares NewShell16 with Calmira
feature by feature. What is still missing:

- Localisation (Calmira's string tables)
- A help file (the Windows help compiler is only in the SDK)
- Disk tools (format, label), drive states in the folder tree
- Drag and drop inside Start Menu Properties
