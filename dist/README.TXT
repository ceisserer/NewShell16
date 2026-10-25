NewShell16 @VERSION@
====================

NewShell16 is a re-implementation of the Calmira shell for Windows 3.1
and Windows for Workgroups 3.11: a taskbar with a Start menu, desktop
icons, folder windows, an Explorer, a Bin, a Task Manager and Find.

It comes with Retheme16, which gives Windows 3.1 the look of Windows 95
(title bars, 3D dialog controls, grey menus, Windows 95 scroll bars).

Requirements: Windows 3.1 or Windows for Workgroups 3.11, Standard or
386 Enhanced mode.


INSTALLING

Run SETUP.EXE (File Manager, or File > Run in Program Manager). Setup
asks for the directory (default C:\NEWSHELL) and can:

  - make NewShell the Windows shell instead of Program Manager
    (SYSTEM.INI shell= and taskman.exe=; Windows is restarted)
  - turn on the Windows 95 look with Retheme16 and pick its theme
  - create a "NewShell" group in Program Manager

Run Setup again (Start > Programs > NewShell > NewShell Setup) to change
these options or to remove NewShell. Removing it puts Program Manager
back as the shell. If NewShell is the shell while Setup has to replace
or remove its files, Setup restarts Windows with Program Manager once,
finishes there and restarts Windows again.

Silent installation (for scripts and network installs):

  SETUP /S [/D=C:\NEWSHELL] [/SHELL | /NOSHELL] [/THEME | /NOTHEME]
        [/T=WIN95] [/NOGROUP] [/NOSTART] [/RESTART]

  /D=dir       installation directory
  /NOSHELL     keep Program Manager as the shell
  /NOTHEME     no Windows 95 look
  /T=theme     theme directory name: WIN95, WIN98 or SLATE
  /NOGROUP     no Program Manager group
  /NOSTART     do not start Retheme16 after installing
  /RESTART     restart Windows if needed

  SETUP /U [/S]   removes NewShell


FILES

  NEWSHELL.EXE   the shell
  NSHOOK.DLL     its system hooks
  RETHEME\       Retheme16 (RETHEME.EXE, RETHEME.DLL, GALLERY.EXE, themes)

NewShell keeps its settings in NEWSHELL.INI and the Start menu in
NSSTART.INI (Calmira's START.INI format), both in the Windows directory.


LICENSE

NewShell16, Retheme16 and Win16Setup are free software under the GNU
General Public License, version 3 or (at your option) any later version;
see COPYING.TXT. NewShell16 is a re-implementation of Calmira (Copyright
Li-Hsin Huang and the Calmira contributors, GPL 2 or later).
