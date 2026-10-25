# NewShell16

A re-implementation of the [Calmira](https://en.wikipedia.org/wiki/Calmira) shell
for Windows 3.1x, written in plain C against the Win16 API.

The original Calmira (by Li-Hsin Huang) was written in Delphi 1 and brought a
Windows 95-style desktop to Windows 3.1: a taskbar with Start menu, desktop
icons, folder windows, shortcuts, a recycle bin and more. NewShell16 aims to
recreate that experience as a small, native 16-bit C program.

## Status

Early bootstrap — only the project skeleton exists so far.

## Goals

- Plain C (C89), no C++ and no runtime frameworks
- Win16 API only; runs on Windows 3.1 / 3.11 (Standard and 386 Enhanced mode)
- Drop-in `SHELL=` replacement for Program Manager via `SYSTEM.INI`

## Planned features (modelled on Calmira)

- Taskbar with Start button, task buttons and tray clock
- Configurable Start menu
- Desktop with icons and shortcuts
- Folder (Explorer-style) windows with drag & drop
- Bin (recycle bin)
- System / Computer window with drives
- Run, Find and Shutdown dialogs
- Settings stored in an INI file

## Building

The intended toolchain is [Open Watcom C 1.9 / 2.0](https://github.com/open-watcom/open-watcom-v2),
which can cross-compile 16-bit Windows binaries from Linux, DOS or Windows:

```sh
wmake
```

## Layout

```
src/    C sources and headers
res/    resource scripts (.rc), icons, bitmaps
docs/   design notes
```
