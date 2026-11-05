DOSLFN 0.42a - long file names for MS-DOS
=========================================

DOSLFN by Henrik Haftmann and Jason Hood gives MS-DOS 4 to 6 the long
file name functions of Windows 95 (INT 21h, functions 71xxh), which
NewShell16 uses for long file names. It needs a 386 or better. Under
Windows 3.1 in 386 Enhanced mode it loads LFNXLAT.386 (from this
directory) by itself.

NewShell16 Setup adds this line to AUTOEXEC.BAT when "Long file names in
DOS" is chosen (and removes it again when the option is turned off or
NewShell16 is removed):

  C:\NEWSHELL\DOSLFN\DOSLFN.COM

The files are those of the DOSLFN 0.42a release, unchanged:
DOSLFN.COM, LFNXLAT.386 and DOSLFN.TXT (the full documentation, with the
options of DOSLFN). DOSLFN is freeware, distributed with its source code:

  https://github.com/adoxa/doslfn

Windows for Workgroups 3.11: with 32-bit file access turned on, DOSLFN
gives hard disks no long names (it falls back to 8.3 names), because
Windows' own file system would bypass it. Turn 32-bit file access off
(Control Panel > 386 Enhanced > Virtual Memory > Change) to use them;
32-bit disk access can stay on.
