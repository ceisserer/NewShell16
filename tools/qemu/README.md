# Windows 3.1 in QEMU, by hand

Two scripts for trying NewShell16 and Retheme16 on a real Windows 3.1 in
QEMU, in a window:

| Script | What it does |
|---|---|
| `mkimage.sh` | Builds the releases of NewShell16 and Retheme16, installs MS-DOS and Windows 3.1 (3.10.103) unattended into a new disk image, and copies both setups onto it, **not installed**: `C:\NSSETUP\SETUP.EXE` (NewShell16, with Retheme16 bundled) and `C:\RTSETUP\SETUP.EXE` (Retheme16 alone). |
| `run.sh` | Starts an image made by `mkimage.sh`. |

Put the installation media next to the scripts (or point `MEDIA=` at
their directory); `*.7z`, `*.zip` and `*.img` here are ignored by git:

```
Microsoft Windows 3.1 (3.10.103) (3.5).7z
Microsoft MS-DOS 6.20 [German] (3.5).7z
Win_31.zip                         only its HIMEM.SYS is used
vbesvga-release.zip                optional, downloaded for -d 8/16
```

Then:

```sh
tools/qemu/mkimage.sh -d 16            # 800x600, 65536 colours (Windows XP look)
tools/qemu/run.sh                      # boots tools/qemu/win31.img
```

In Windows, File > Run `C:\NSSETUP\SETUP.EXE` in Program Manager installs
NewShell16 (choose "Windows 95 or XP look" and the RedmondExperience theme
for the XP look). Setup restarts Windows with NewShell16 as the shell.

`mkimage.sh` options: `-o IMAGE`, `-d 4|8|16` (16 colours, the default;
256; 65536 with the VBE SVGA driver), `-s 1024x768`, `-r DIR` (bundle
another Retheme16 checkout instead of the submodule, e.g. a branch with
new themes), `-n` (skip the build, use the releases in `build/release`).

`run.sh` options: `-t` (throw changes away when QEMU ends), `-f DISK.IMG`
(a floppy as drive A:), `-m MB`; options after the image go to QEMU
(e.g. `-vnc :1`). Click into the window to give QEMU the mouse,
Ctrl+Alt+G releases it. Shut Windows down before closing the window.

The image keeps every change; run `mkimage.sh` again for a fresh one (about
a minute). For scripted screenshot tests see [docs/testing.md](../../docs/testing.md).
