NewShell16 @VERSION@
====================

NewShell16 is a re-implementation of the Calmira shell for Windows 3.1
and Windows for Workgroups 3.11: a taskbar with a Start menu, desktop
icons, folder windows, a File System window, a Bin, a Task Manager and Find.

It comes with Retheme16, which gives Windows 3.1 the look of Windows 95
(title bars, 3D dialog controls, grey menus, Windows 95 scroll bars).

Requirements: Windows 3.1 or Windows for Workgroups 3.11, Standard or
386 Enhanced mode.


INSTALLING

Run SETUP.EXE (File Manager, or File > Run in Program Manager). Setup
asks for the directory (default C:\NEWSHELL) and can:

  - make NewShell16 the Windows shell instead of Program Manager
    (SYSTEM.INI shell= and taskman.exe=; Windows is restarted)
  - turn on the Windows 95 look with Retheme16 and pick its theme
  - create a "NewShell16" group in Program Manager

Run Setup again (Start > Programs > NewShell16 > NewShell16 Setup) to change
these options or to remove NewShell16. Removing it puts Program Manager
back as the shell. If NewShell16 is the shell while Setup has to replace
or remove its files, Setup restarts Windows with Program Manager once,
finishes there and restarts Windows again.

"Themes" in the same group switches the theme of the Windows 95 look at
once, without restarting Windows.

Silent installation (for scripts and network installs):

  SETUP /S [/D=C:\NEWSHELL] [/SHELL | /NOSHELL] [/THEME | /NOTHEME]
        [/T=WIN95] [/NOGROUP] [/NOSTART] [/RESTART]

  /D=dir       installation directory
  /NOSHELL     keep Program Manager as the shell
  /NOTHEME     no Windows 95 look
  /T=theme     theme directory name: WIN95, WIN98, WINME or SLATE
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


FILES

  NEWSHELL.EXE   the shell
  NSHOOK.DLL     its system hooks
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
