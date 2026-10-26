# Reference media

Original Microsoft software used for testing NewShell16 and as API
reference. None of it is part of this repository and it must not be
committed; obtain your own copies.

| File | Size | Use |
|------|------|-----|
| `Windows 3.11.iso` | 11 MB | Windows 3.11 install media |
| `Win_31.zip` | 6.2 MB | Pre-installed `C:\WINDOWS` tree (Windows 3.1, VGA) — used by the emulator harness |
| `Microsoft Windows 3.1 [German] (3.5-1.44mb).7z` | 6.3 MB | German Windows 3.1 floppies (localisation testing) |
| `Microsoft Windows 3.1 (3.10.103) (3.5).7z` | 6.4 MB | Windows 3.1 floppies |
| `Microsoft MS-DOS 6.20 [German] (3.5).7z` | 4.2 MB | German MS-DOS 6.20 floppies |
| `Microsoft Windows 3.1 SDK (1992) (ISO).7z` | 17 MB | Windows 3.1 SDK: headers, API documentation, samples, the MS Setup toolkit (how setup programs talk to Program Manager) and the help compiler `HC31.EXE` |
| `Microsoft Windows 3.1 DDK.7z` | 86 MB | Windows 3.1 Device Driver Kit |
| `Microsoft Windows For Workgroups 3.11 (OEM) [German] (3.5-1.44mb).7z` | 11 MB | German WfW 3.11 floppies |
| `Microsoft Visual C++ - Version 1.52c (Microsoft) (1995).iso` | 202 MB | Not used — NewShell16 is built with Open Watcom |

The quickest way to get a test system is `Win_31.zip`: unpack it and pass
its `Win/WINDOWS` directory to the emulator harness (see
[testing.md](testing.md)).
