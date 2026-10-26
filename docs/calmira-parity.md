# Calmira feature parity

This compares NewShell16 with Calmira II Online! Release 3.3, the last
release, whose Delphi source is at
[RauliL/calmira](https://github.com/RauliL/calmira). Each Calmira feature
was taken from its forms (`SRC/*.DFM`: menus, dialogs and option lists)
and units, and checked against NewShell16.

- **Yes**: NewShell16 has it (possibly in a different place).
- **Partly**: a simpler version.
- **No**: missing.

Settings named below are keys in `NEWSHELL.INI`; nearly all of them are
in the Options dialog (see [design.md](design.md)).

## Look and artwork

| Calmira | NewShell16 | Notes |
|---|---|---|
| Icons and small images (`ICONS.RES`, `BMP/`) | Yes | Imported with `tools/import-calmira.py` into `res/calmira/`. Images showing third-party logos are left out. The NewShell16 icon and the small Start button, Task Manager, About, DOS and open folder images are NewShell16's own. |
| 3D Start menu: large icons on the first level, banner, button-face colours, raised frame | Yes | The banner reads "NewShell16" on a gradient in the caption colour; Calmira uses a bitmap (`BANNER.BMP`). |
| Start menu options: coloured or plain highlight bar, bold selected item, large item height, plain Windows 3.1 menu | Yes | Start Menu Properties. |
| White submenu arrow on the selected item | No | Windows 3.1 paints the arrow area of a selected owner-drawn item after `WM_DRAWITEM`; Calmira has the same gap. |
| Flat toolbar buttons (option) | Yes | `[Folders] FlatToolbar=1`. |
| Separate Start menu font | Yes | In Start Menu Properties (Calmira has it in Desktop Properties). |
| Custom colours for frames, selection, shortcut arrows, taskbar, Start menu | Partly | Taskbar, Start menu and selection (`[Colors]`); frames and arrows keep the system colours. |
| Splash screen, `SHUTDOWN.BMP` screen while shutting down | Yes | Drawn, not bitmaps: a gradient with NewShell16's name, and "Windows is shutting down...". |
| Tip of the Day | Yes | Help menu, `$Tips`, and at start-up with `ShowTips=1`. |

## Start menu

| Calmira | NewShell16 | Notes |
|---|---|---|
| `START.INI` format with `$` macros and `<image>` names | Yes | `NSSTART.INI`, same format. |
| Macros `$Run`, `$Shutdown`, `$Find`, `$Explore`, `$System`, `$Bin`, `$Folder` (with large/small/list variants), `$MinimizePrograms`, `$ArrangeIcons`, `$LineUpIcons`, `$Help`, `$...Prop` | Yes | `$TaskbarProp`, `$DesktopProp`, `$FileSystemProp` and `$SystemProp` open their page of the Options; `$StartMenuProp` and `$BinProp` their own dialogs. |
| Macros `$CascadeBrowsers`, `$CloseBrowsers`, `$ClearDesktop` | Yes | Also `$BrowseFolders`, `$ScreenSaver`, `$ToggleTaskbar`, `$Tips`. |
| Start Menu Properties: tree editor for items (add, insert, delete, item properties, expand/collapse) | Yes | New item, submenu or separator, delete, properties (with Change Icon), move up and down. No drag and drop inside the tree. |
| Import Program Manager groups / export a group | Partly | Import Groups adds the groups that are missing; no export (the groups of setup programs arrive over DDE). |
| Program Manager DDE server, so installers can add groups and items | Yes | As the shell; `CreateGroup`, `ShowGroup`, `AddItem`, `ReplaceItem`, `DeleteItem`, `DeleteGroup`, `Reload`, `ExitProgman`, and requests for `Groups` and a group. |
| Open on mouse-up instead of mouse-down (option) | Yes | Start Menu Properties. The keyboard only works in a menu opened with the mouse in this mode (the menu waits for the release otherwise), or with Ctrl+Esc and the Windows key. |
| Windows 95 Start key | Yes | Opens the menu when released; Win+E, Win+R, Win+F, Win+M, Win+D, Win+Tab. |
| Internet shortcuts (URL items, `<internet>` image) | Yes | `http://`, `ftp://`, `www.`... open in `[Internet] Browser=` or the `.HTM` program. |
| Run StartUp items only when Calmira is the shell (option) | Yes | Always only when NewShell16 is the shell; the StartUp group of the Start menu once it exists. |

## Taskbar

| Calmira | NewShell16 | Notes |
|---|---|---|
| Task buttons, clock, real system menus on buttons | Yes | |
| Fit maximised windows above the taskbar | Yes | |
| Hide icons of minimised applications | Yes | |
| Move minimised window icons above the bar (instead of hiding them) | Yes | `MinAbove=1` with `HideMinApps=0`. |
| Auto-hide with animation (speed, slow-down, show controls while animating) | Partly | Slides in `AnimSteps=` steps; no slow-down. |
| Hide and disable the taskbar | Yes | `Enabled=0`, Hide the Taskbar on its menu. |
| 24-hour clock (option) | Yes | Follows the Control Panel time format (`[intl]` in `WIN.INI`). |
| Free resources or the full date over the clock | Yes | A hint with the date, free memory and resources. |
| Full window caption as a button hint | Yes | |
| Buttons for icon windows and the Explorer, full path on icon window buttons | Yes | Folder windows are ordinary task buttons, titled with their path. |
| Document name before the program name, lower-case document names | Yes | `DocFirst=1`, `LowerDoc=1`. |
| Applet tray: programs started into a tray area | Yes | `Tray=` lists programs shown as icons beside the clock. |
| Exclusion list of programs without a button | Yes | `Exclude=`. |
| Task menu: hide/show the taskbar, minimise all, window info, terminate | Yes | Window Info and Terminate on the window menu of a button. |

## Desktop

| Calmira | NewShell16 | Notes |
|---|---|---|
| Computer and Bin icons, right-click menu on the wallpaper | Yes | |
| Shortcuts: drag files, folders, drives or programs onto the desktop | Yes | From folder windows and from File Manager. |
| Shortcut properties (caption, icon, working directory, show mode) | Yes | With a Change Icon dialog. |
| Arrow symbols on shortcuts and aliases | Yes | Calmira's arrow. |
| Arrange / restore / line up shortcuts, snap to grid | Yes | Arrange Icons, Line up Icons, `SnapToGrid=1`. |
| Clear Desktop (minimise everything, toggle) and `SHOWDESK.EXE` | Yes | Show the Desktop; `NEWSHELL /D` instead of a separate program. |
| Cascade / minimise / close icon windows | Yes | Cascade and Close Folder Windows. |
| Activate shortcuts with a single click (option) | Yes | `SingleClick=1`. |
| Where new icon windows open: cascaded, random, last position | Partly | Windows' place, cascaded or the last position (`WindowPos=`); no random. |
| Save open windows between sessions | Yes | `[Folders] SaveWindows=1`. |
| Right-click on minimise/maximise box closes the window | Yes | `RightClose=1`. |

## Folder windows ("icon windows")

| Calmira | NewShell16 | Notes |
|---|---|---|
| Large icons, small icons, details; sort by name, type, size, date | Yes | Plus a list view. |
| Toolbar in icon windows (path combo box, parent, new folder, cut/copy/paste, delete, rename, properties, views, sort) | Partly | In every folder window, with Go To and Rename; a Go To dialog with history instead of the path box (combo boxes in ordinary windows are not themed by Retheme16). |
| Status bar (one or two sections) | Yes | |
| Aliases (`.ALS` files pointing at another file) and File > New > Alias | Yes | Calmira's file format; Create Alias. |
| Open With... | Yes | Also offered for files without an association. |
| Inspect (open with a configured viewer) | Yes | `[Folders] Inspect=`, default Notepad. |
| Describe (edit `DESCRIPT.ION` / 4DOS descriptions) | Yes | |
| Select Files by wildcard, Set Filter (wildcards, show hidden) | Yes | |
| Go To folder dialog | Yes | |
| Incremental search | Yes | Type-ahead. |
| Details columns can be switched off | Yes | View > Columns. |
| Popup file hints (description, size, type, date, attributes) | Yes | |
| Shade selected icons, capitalise names, hide extensions of files with their own icon | Yes | |
| Drop files onto program icons to open them with that program | Yes | In folders and on desktop shortcuts. |
| Start drag and drop in copy mode, ask Copy/Move on drop | Yes | `DragCopy=1`, `AskOnDrop=1`. |
| Browse through folders in the same window | Yes | Opposite of the New windows option. |
| Map / disconnect network drive | Yes | Windows' network dialogs, when the network driver has them. |
| Undelete (Ctrl+U), print documents, programs for disk properties | Partly | Undelete and Print; no disk programs. |
| Rubber-band selection, dragging with the right mouse button | Yes | |

## Explorer (File System window)

| Calmira | NewShell16 | Notes |
|---|---|---|
| Folder tree beside a folder view, toolbar, status bar | Yes | |
| Separate Browse Folders tree window with its own menu | Yes | |
| Tree context menu: explore, open in new window, find, refresh, expand one level / branch / all, collapse | Yes | |
| Drive icons in the tree with open/closed folders and drive states | Partly | Drive icons and open and closed folders; no drive states. |
| Long filenames per drive (Windows 95 LFN API under DOS 7) | No | NewShell16 is for Windows 3.1, which has no long file names. |
| Share as / stop sharing | Yes | With Windows for Workgroups. |
| Open the Explorer at the last folder explored (option) | Yes | `[Explorer] RememberLast=1`. |

## File operations

| Calmira | NewShell16 | Notes |
|---|---|---|
| Copy, move, delete, rename, new folder, progress dialog, replace dialog | Yes | |
| Separate confirmations (start copy/move/delete, each file, folders, protected files) | Yes | |
| Yes to All / No to All when replacing | Yes | |
| File associations: Association tab, double-click on unassociated files runs a program | Yes | Properties > Opens with > Change; Open With for unassociated files. |

## Find

| Calmira | NewShell16 | Notes |
|---|---|---|
| Name, start folder, subfolders, containing text | Yes | |
| Match case for text | Yes | |
| Result menu: open, open with, open parent, delete, copy filenames, copy details | Yes | Plus Properties. |
| Icons next to results (option) | Yes | `[Find] Icons=`. |

## Properties

| Calmira | NewShell16 | Notes |
|---|---|---|
| Files, folders, drives, several items, attributes | Yes | |
| Folder size by deep scan | Yes | |
| Version tab (file version, description, copyright, others) | Yes | A Version button (Windows 3.1 has no tabs). |
| Association tab | Yes | "Opens with", with Change. |
| Drive usage pie chart | Yes | |

## Bin

| Calmira | NewShell16 | Notes |
|---|---|---|
| Restore, delete, empty | Yes | |
| Properties: caption, small icons, capacity in MB, empty on shutdown, delete instead of storing | Yes | The Bin window always uses small icons. |

## System and other programs

| Calmira | NewShell16 | Notes |
|---|---|---|
| Task Manager replacement (`CTASKMAN.EXE`) | Yes | Built in; Ctrl+Esc via `taskman.exe=`. |
| Users: logon dialog, passwords, settings per user | Yes | `[Users] Enabled=1`; the passwords keep settings apart, they are not security. |
| Sounds for events | Yes | Start-up, exit, Start menu, opening a folder, emptying the Bin. |
| Global hot keys | Yes | `[Hotkeys]`. |
| Internet: URL prefixes, browser choice | Yes | |
| MS-DOS and Windows scripts, DOS command / DOS session from Run | Partly | Run in an MS-DOS window; batch files run as usual; no script language. |
| Run dialog history list | Yes | |
| Shut Down: exit, quit the shell, restart Windows, reboot | Yes | Plus Log Off with users. |
| Screen saver launcher (`SCRNSAVE.EXE`) | Yes | `$ScreenSaver`, desktop menu. |
| Computer window caption option, restore the Computer window after loading, quit when it closes | Yes | |
| Online help (`CALMIRA.HLP`) | Partly | Help opens `NEWSHELL.HLP` if one is installed, else `README.TXT`; the Windows help compiler is part of the SDK, so the build cannot make a help file. |
| Localisation (string tables in `LOCALE.RES`) | No | English only. |
