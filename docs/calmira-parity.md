# Calmira feature parity

This compares NewShell16 with Calmira II Online! Release 3.3, the last
release, whose Delphi source is at
[RauliL/calmira](https://github.com/RauliL/calmira). Each Calmira feature
was taken from its forms (`SRC/*.DFM`: menus, dialogs and option lists)
and units, and checked against NewShell16.

- **Yes**: NewShell16 has it (possibly in a different place).
- **Partly**: a simpler version.
- **No**: missing.

## Look and artwork

| Calmira | NewShell16 | Notes |
|---|---|---|
| Icons and small images (`ICONS.RES`, `BMP/`) | Yes | Imported with `tools/import-calmira.py` into `res/calmira/`. Images showing third-party logos are left out. The NewShell16 icon and the small Start button, Task Manager, About and DOS images are NewShell16's own. |
| 3D Start menu: large icons on the first level, banner, button-face colours, raised frame | Yes | The banner reads "NewShell16" on a gradient in the caption colour; Calmira uses a bitmap (`BANNER.BMP`). |
| Start menu options: coloured or plain highlight bar, bold selected item, large item height, plain Windows 3.1 menu | No | Always the coloured bar, normal font and 34-pixel rows. |
| White submenu arrow on the selected item | No | Windows 3.1 paints the arrow area of a selected owner-drawn item after `WM_DRAWITEM`; Calmira has the same gap. |
| Flat toolbar buttons (option) | No | |
| Separate Start menu font (Desktop Properties) | No | The Start menu uses the menu font. |
| Custom colours for frames, selection, shortcut arrows, taskbar, Start menu | No | NewShell16 follows the system colours. |
| Splash screen, `SHUTDOWN.BMP` screen while shutting down | No | |
| Tip of the Day | No | |

## Start menu

| Calmira | NewShell16 | Notes |
|---|---|---|
| `START.INI` format with `$` macros and `<image>` names | Yes | `NSSTART.INI`, same format. |
| Macros `$Run`, `$Shutdown`, `$Find`, `$Explore`, `$System`, `$Bin`, `$Folder` (with large/small/list variants), `$MinimizePrograms`, `$ArrangeIcons`, `$LineUpIcons`, `$Help`, `$...Prop` | Yes | The `$...Prop` macros all open the Options dialog. |
| Macros `$CascadeBrowsers`, `$CloseBrowsers`, `$ClearDesktop` | No | Reported as unsupported. |
| Start Menu Properties: tree editor for items (add, insert, delete, item properties, expand/collapse) | No | `NSSTART.INI` has to be edited by hand. |
| Import Program Manager groups / export a group | Partly | Groups are imported once, when `NSSTART.INI` does not exist. |
| Program Manager DDE server, so installers can add groups and items | No | Installers that talk to `PROGMAN` find no server while NewShell16 is the shell. |
| Open on mouse-up instead of mouse-down (option) | No | Opens on mouse-down and stays open. |
| Windows 95 Start key | Yes | |
| Internet shortcuts (URL items, `<internet>` image) | Partly | The image exists; URLs are not opened with a browser. |
| Run StartUp items only when Calmira is the shell (option) | Yes | Always only when NewShell16 is the shell. |

## Taskbar

| Calmira | NewShell16 | Notes |
|---|---|---|
| Task buttons, clock, real system menus on buttons | Yes | |
| Fit maximised windows above the taskbar | Yes | |
| Hide icons of minimised applications | Yes | |
| Move minimised window icons above the bar (instead of hiding them) | No | |
| Auto-hide with animation (speed, slow-down, show controls while animating) | No | |
| Hide and disable the taskbar | No | |
| 24-hour clock (option) | Yes | Follows the Control Panel time format (`[intl]` in `WIN.INI`). |
| Free resources or the full date over the clock | No | |
| Full window caption as a button hint | No | |
| Buttons for icon windows and the Explorer, full path on icon window buttons | Yes | Folder windows are ordinary task buttons, titled with their path. |
| Document name before the program name, lower-case document names | No | |
| Applet tray: programs started into a tray area | No | |
| Exclusion list of programs without a button | No | Only the theme engine's window is excluded. |
| Task menu: hide/show the taskbar, minimise all, window info, terminate | Partly | Minimise all, cascade and tile, and the Task Manager are there. |

## Desktop

| Calmira | NewShell16 | Notes |
|---|---|---|
| Computer and Bin icons, right-click menu on the wallpaper | Yes | |
| Shortcuts: drag files, folders, drives or programs onto the desktop | No | Shortcuts are `ShortcutN=` lines in `NEWSHELL.INI`. |
| Shortcut properties (caption, icon, working directory, show mode) | No | |
| Arrow symbols on shortcuts and aliases | No | |
| Arrange / restore / line up shortcuts, snap to grid | Partly | Arrange Icons only. |
| Clear Desktop (minimise everything, toggle) and `SHOWDESK.EXE` | Partly | Minimise All Windows, no toggle. |
| Cascade / minimise / close icon windows | Partly | Cascade and tile cover all windows. |
| Activate shortcuts with a single click (option) | No | |
| Where new icon windows open: cascaded, random, last position | No | Windows choose the position. |
| Save open windows between sessions | No | |
| Right-click on minimise/maximise box closes the window | No | |

## Folder windows ("icon windows")

| Calmira | NewShell16 | Notes |
|---|---|---|
| Large icons, small icons, details; sort by name, type, size, date | Yes | Plus a list view. |
| Toolbar in icon windows (path combo box, parent, new folder, cut/copy/paste, delete, rename, properties, views, sort) | Partly | The toolbar is only in the File System window, without a path box or Rename button. |
| Status bar (one or two sections) | Yes | |
| Aliases (`.ALS` files pointing at another file) and File > New > Alias | No | |
| Open With... | No | |
| Inspect (open with a configured viewer) | No | |
| Describe (edit `DESCRIPT.ION` / 4DOS descriptions) | Partly | Descriptions are shown in details view, not edited. |
| Select Files by wildcard, Set Filter (wildcards, show hidden) | No | Invert Selection and the Show hidden files option exist. |
| Go To folder dialog | No | |
| Incremental search | Yes | Type-ahead. |
| Details columns can be switched off | No | Columns can be resized, not hidden. |
| Popup file hints (description, size, type, date, attributes) | No | |
| Shade selected icons, capitalise names, hide extensions of files with their own icon | No | |
| Drop files onto program icons to open them with that program | No | |
| Start drag and drop in copy mode, ask Copy/Move on drop | No | Ctrl copies, as in File Manager. |
| Browse through folders in the same window | Yes | Opposite of the New windows option. |
| Map / disconnect network drive | No | |
| Undelete (Ctrl+U), print documents, programs for disk properties | No | |
| Rubber-band selection, dragging with the right mouse button | No | Also listed in the roadmap. |

## Explorer (File System window)

| Calmira | NewShell16 | Notes |
|---|---|---|
| Folder tree beside a folder view, toolbar, status bar | Yes | |
| Separate Browse Folders tree window with its own menu | No | |
| Tree context menu: explore, open in new window, find, refresh, expand one level / branch / all, collapse | No | |
| Drive icons in the tree with open/closed folders and drive states | Partly | Drive icons, but one folder image. |
| Long filenames per drive (Windows 95 LFN API under DOS 7) | No | |
| Share as / stop sharing | No | |
| Open the Explorer at the last folder explored (option) | No | |

## File operations

| Calmira | NewShell16 | Notes |
|---|---|---|
| Copy, move, delete, rename, new folder, progress dialog, replace dialog | Yes | |
| Separate confirmations (start copy/move/delete, each file, folders, protected files) | Partly | One Confirm deleting option. |
| Yes to All / No to All when replacing | Partly | Yes to All only. |
| File associations: Association tab, double-click on unassociated files runs a program | No | Windows' `[Extensions]` associations are used. |

## Find

| Calmira | NewShell16 | Notes |
|---|---|---|
| Name, start folder, subfolders, containing text | Yes | |
| Match case for text | No | |
| Result menu: open, open with, open parent, delete, copy filenames, copy details | Partly | Results can be opened. |
| Icons next to results (option) | No | |

## Properties

| Calmira | NewShell16 | Notes |
|---|---|---|
| Files, folders, drives, several items, attributes | Yes | |
| Folder size by deep scan | No | |
| Version tab (file version, description, copyright, others) | No | |
| Association tab | No | |
| Drive usage pie chart | No | |

## Bin

| Calmira | NewShell16 | Notes |
|---|---|---|
| Restore, delete, empty | Yes | |
| Properties: caption, small icons, capacity in MB, empty on shutdown, delete instead of storing | No | |

## System and other programs

| Calmira | NewShell16 | Notes |
|---|---|---|
| Task Manager replacement (`CTASKMAN.EXE`) | Yes | Built in; Ctrl+Esc via `taskman.exe=`. |
| Users: logon dialog, passwords, settings per user | No | |
| Sounds for events | No | |
| Global hot keys | No | |
| Internet: URL prefixes, browser choice | No | |
| MS-DOS and Windows scripts, DOS command / DOS session from Run | No | |
| Run dialog history list | No | |
| Shut Down: exit, quit the shell, restart Windows, reboot | Yes | |
| Screen saver launcher (`SCRNSAVE.EXE`) | No | |
| Computer window caption option, restore the Computer window after loading, quit when it closes | No | |
| Online help (`CALMIRA.HLP`) | No | Help opens Windows Help. |
| Localisation (string tables in `LOCALE.RES`) | No | English only. |

## Suggested order

1. Program Manager DDE server: without it, installers cannot add their
   groups while NewShell16 is the shell.
2. Desktop shortcuts by drag and drop, with shortcut properties and
   arrows.
3. Open With, Select Files and Set Filter, and the toolbar in folder
   windows.
4. A Start menu editor (the Start Menu Properties tree).
5. Tree context menu and Browse Folders.
6. Taskbar auto-hide, tray and exclusion list.
7. Bin properties and capacity.
