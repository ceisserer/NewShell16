NewShell16 @VERSION@
====================

NewShell16 is a re-implementation of the Calmira shell for Windows 3.1
and Windows for Workgroups 3.11: a taskbar with a Start menu, desktop
icons, folder windows, a File System window, a Bin, a Task Manager and Find.

It comes with Retheme16, which gives Windows 3.1 the look of Windows 95
(title bars, 3D dialog controls, grey menus, Windows 95 scroll bars), or
with its RedmondExperience theme the blue look of Windows XP; NewShell16's
taskbar and Start menu then turn into the Windows XP ones too (green Start
button, blue taskbar). The XP look is made for display drivers with 65536
or more colours; with 16 or 256 colours Windows uses the nearest colours.

Requirements: Windows 3.1 or Windows for Workgroups 3.11, Standard or
386 Enhanced mode.


INSTALLING

Run SETUP.EXE (File Manager, or File > Run in Program Manager). Setup
asks for the directory (default C:\NEWSHELL) and can:

  - make NewShell16 the Windows shell instead of Program Manager
    (SYSTEM.INI shell= and taskman.exe=; Windows is restarted)
  - turn on the Windows 95 or XP look with Retheme16 and pick its theme
  - create a "NewShell16" group in Program Manager

Run Setup again (Start > Programs > NewShell16 > NewShell16 Setup) to change
these options or to remove NewShell16. Removing it puts Program Manager
back as the shell. If NewShell16 is the shell while Setup has to replace
or remove its files, Setup restarts Windows with Program Manager once,
finishes there and restarts Windows again.

"Themes" in the same group switches the theme at once, without
restarting Windows. Start > Settings > Taskbar, page Desktop, "Taskbar
look" chooses the Windows XP or Windows 95 look of NewShell16's taskbar
and Start menu regardless of the theme.

Silent installation (for scripts and network installs):

  SETUP /S [/D=C:\NEWSHELL] [/SHELL | /NOSHELL] [/THEME | /NOTHEME]
        [/T=WIN95] [/LFN | /NOLFN] [/NOGROUP] [/NOSTART] [/RESTART]

  /D=dir       installation directory
  /NOSHELL     keep Program Manager as the shell
  /NOTHEME     no Windows 95 look
  /T=theme     theme directory name: WIN95, WIN98, WINME, WINXP or SLATE
  /LFN         long file names in DOS: DOSLFN in AUTOEXEC.BAT (restarts the
               computer with /RESTART)
  /NOGROUP     no Program Manager group
  /NOSTART     do not start Retheme16 after installing
  /RESTART     restart Windows if needed

  SETUP /U [/S]   removes NewShell16


USING NEWSHELL16

  Start menu     Start > Settings > Start Menu edits it. Setup programs add
                 their groups to Programs, as they would to Program Manager.
  Shortcuts      Drag files, folders, drives or programs from a folder
                 window or File Manager onto the desktop. Right-click a
                 shortcut for its properties; drop it on the Bin to remove it.
  Dragging       With the right mouse button, a menu asks whether to move,
                 copy or make an alias. Drop documents onto a program to
                 open them with it.
  Folders        View > Set Filter, Edit > Select Files, File > Open With,
                 Describe, Create Alias, Ctrl+U undeletes, Ctrl+G goes to a
                 folder. Rest the mouse on a file to see its details.
  Taskbar        Right-click it for Show the Desktop, Auto Hide and the
                 Task Manager. Ctrl+Esc opens the Task Manager or the Start
                 menu; the Windows key the Start menu (Win+E, Win+R, Win+F,
                 Win+M, Win+D).
  Options        Start > Settings > Taskbar has a page for each part of
                 NewShell16. Some settings, such as the tray, hot keys and
                 sounds, are explained in the NewShell16 documentation
                 (docs/design.md in the source).


LONG FILE NAMES

NewShell16 shows and keeps long file names ("Letter to the tax
office.txt") when a long file name driver is loaded before Windows:

  - DOSLFN, which comes with NewShell16: Setup's option "Long file names
    in DOS" adds C:\NEWSHELL\DOSLFN\DOSLFN.COM to AUTOEXEC.BAT and
    restarts the computer (DOSLFN\README.TXT; needs a 386)
  - LFNDOS (free, GPL; https://www.ibiblio.org/pub/micro/pc-stuff/freedos/
    files/repositories/latest/pkg-html/lfndos.html): LFNDOS in AUTOEXEC.BAT
  - or Windows 95's own MS-DOS 7, and DOSBox-X ([dos] lfn=true)

Folder windows, the File System tree, Find, Properties, the Bin, Go To and
Run then use long names: renaming, New Folder ("New Folder"), copying and
moving keep them. Windows 3.1 programs still see the 8.3 names
(LETTER~1.TXT). Without a driver everything works with 8.3 names, as
before. Options, page Files, "Long file names" switches them off.

With Windows for Workgroups 3.11, turn 32-bit file access off (Control
Panel > 386 Enhanced > Virtual Memory > Change) when a DOS long file name
driver is used: Windows' own file system would bypass the driver.


OPEN AND SAVE AS DIALOGS

NSDLG.DLL gives every Windows 3.1 program the Open and Save As dialogs of
Windows 95, Windows XP (places bar) or Windows 11 (navigation pane,
address bar, search), following the theme, with long file names. The
program gets 8.3 names back: saving under a new long name creates the file
first, so it has its 8.3 alias (as Windows 95 does). Programs that bring a
dialog template or hook of their own (Write, Paintbrush...) keep the
Windows 3.1 dialog. Options, page Files, "Open and Save As" chooses the
look, or the Windows 3.1 dialogs for every program.


FILES

  NEWSHELL.EXE   the shell
  NSHOOK.DLL     its system hooks
  NSDLG.DLL      the Open and Save As dialogs of Windows 95, XP and 11
  DOSLFN\        DOSLFN 0.42a, long file names for DOS (freeware with source,
                 https://github.com/adoxa/doslfn)
  RETHEME\       Retheme16 (RETHEME.EXE, RETHEME.DLL, THEMES.EXE, GALLERY.EXE,
                 themes)

NewShell16 keeps its settings in NEWSHELL.INI and the Start menu in
NSSTART.INI (Calmira's START.INI format), both in the Windows directory.


LICENSE

NewShell16, Retheme16 and Win16Setup are free software under the GNU
General Public License, version 3 or (at your option) any later version;
see COPYING.TXT. NewShell16 is a re-implementation of Calmira (Copyright
Li-Hsin Huang and the Calmira contributors, GPL 2 or later), and uses most
of Calmira's icons.
