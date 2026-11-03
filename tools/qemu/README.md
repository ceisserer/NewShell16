# Windows 3.1 in QEMU, by hand

Two scripts for trying NewShell16 and Retheme16 on a real Windows 3.1 in
QEMU, in a window:

| Script | What it does |
|---|---|
| `mkimage.sh` | Installs MS-DOS and Windows 3.1 (3.10.103) unattended into a new disk image and copies the prebuilt NewShell16 and Retheme16 setups onto it, **not installed**: `C:\NSSETUP\SETUP.EXE` (NewShell16, with Retheme16 bundled) and `C:\RTSETUP\SETUP.EXE` (Retheme16 alone). |
| `run.sh` | Starts an image made by `mkimage.sh`. |

Put the installation media and the release packages next to the scripts
(or point `MEDIA=` at their directory); `*.7z`, `*.zip` and `*.img` here are
ignored by git:

```
Microsoft Windows 3.1 (3.10.103) (3.5).7z
Microsoft MS-DOS 6.20 [German] (3.5).7z
Win_31.zip                         only its HIMEM.SYS is used
newshell-X.Y.Z.zip                 NewShell16 release
retheme16-X.Y.Z.zip                Retheme16 release (optional, NewShell16 bundles it)
vbesvga-release.zip                optional, downloaded for -d 8/16
```

The release packages come from the GitHub Releases pages of
[NewShell16](https://github.com/ceisserer/NewShell16/releases) and
[Retheme16](https://github.com/ceisserer/win31_theme_engine/releases) (the
`snapshot` pre-releases follow the main branches), from the artifacts of a
CI build, or from `wmake release` / `make release`. The floppy images
(`*-disk.img`) and unpacked release directories work too; the highest
version is used.

Tools (Fedora; on Debian/Ubuntu `p7zip-full fdisk` instead of `p7zip
p7zip-plugins util-linux`):

```sh
sudo dnf install qemu-system-x86 p7zip p7zip-plugins mtools util-linux unzip curl git python3
```

The emulator tools come from the Retheme16 submodule, which is fetched if
it is missing.

Then:

```sh
tools/qemu/mkimage.sh -d 16            # 800x600, 65536 colours (Windows XP look)
tools/qemu/run.sh                      # boots tools/qemu/win31.img
```

In Windows, File > Run `C:\NSSETUP\SETUP.EXE` in Program Manager installs
NewShell16 (choose "Windows 95 or XP look" and the RedmondExperience theme
for the XP look). Setup restarts Windows with NewShell16 as the shell.

`mkimage.sh` options: `-o IMAGE`, `-d 4|8|16` (16 colours, the default;
256; 65536 with the VBE SVGA driver), `-s 1024x768`, `-b` (build the
setups from source instead of using release packages), `-r DIR` (with
`-b`: bundle another Retheme16 checkout instead of the submodule, e.g. a
branch with new themes).

`-b` needs gcc, make, Python's Pillow and Open Watcom 2.0, which is taken
from `$WATCOM`, `/opt/watcom` or `~/.cache/newshell16/watcom`; if none has
it, the current snapshot (about 100 MB) is downloaded into the last one.

`run.sh` options: `-t` (throw changes away when QEMU ends), `-f DISK.IMG`
(a floppy as drive A:), `-m MB`, `-d gtk|sdl` (QEMU's window, GTK by
default), `-n` (no KVM); options after the image go to QEMU. Click into
the window to give QEMU the mouse, Ctrl+Alt+G releases it. Shut Windows
down before closing the window.

Speed: `run.sh` uses KVM when `/dev/kvm` is readable and writable
(`sudo usermod -aG kvm $USER`, then log in again); without it QEMU
emulates the CPU and says so.

VNC (`run.sh IMAGE -vnc :1 -display none`) works but is slower than
QEMU's own window, and the pointer can be out of step: Windows 3.1 only
has a relative PS/2 mouse, so QEMU turns the VNC client's positions into
moves. Images made by `mkimage.sh` have mouse acceleration off
(`MouseSpeed=0` in WIN.INI; for an older image add that line to
`[windows]` in `C:\WINDOWS\WIN.INI`), so the offset stays constant;
move the pointer to the edge that Windows' pointer reaches first (it
stops there, the VNC pointer catches up) and both line up. VNC clients
that can grab the pointer, such as `remote-viewer vnc://HOST:5901`
(virt-viewer), avoid the problem.

The image keeps every change; run `mkimage.sh` again for a fresh one (about
a minute). For scripted screenshot tests see [docs/testing.md](../../docs/testing.md).
