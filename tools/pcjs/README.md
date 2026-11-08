# Windows 3.1 with NewShell16 in the browser (PCjs)

`mkpcjs.py` builds a static web page that runs MS-DOS 6.20, Windows 3.1
and NewShell16 in [PCjs](https://www.pcjs.org)'s PCx86 emulator, as:

| | |
|---|---|
| Machine | IBM PC AT: 80286 at 12 MHz, 4 MB RAM (640 KB + 3456 KB extended) |
| Display | IBM VGA, Windows' VGA driver: 640x480, 16 colours |
| Windows | 3.1 (3.10.103) in standard mode (a 286 has no 386 enhanced mode) |
| Disk | 20 MB, IBM AT drive type 2, about 9 MB free |
| Mouse | serial (COM1), keyboard US 84 keys |
| Shell | NewShell16 with the Windows 95 look of Retheme16 |

```sh
MEDIA=~/win31 tools/pcjs/mkpcjs.py           # writes build/pcjs
python3 -m http.server -d build/pcjs 8000    # http://localhost:8000/
```

`build/pcjs` is the whole site; copy it to any static web server (no
server-side code, no special MIME types).

```
index.html          the page with the machine
pcx86.js            PCjs PCx86 2.26 (MIT licence, Jeff Parsons)
components.css      PCjs's machine styles
rom/ATBIOS-REV3.json5   IBM PC AT BIOS (1985-11-15)
rom/IBM-VGA.json5       IBM VGA BIOS (1986-10-27)
win31.img           the hard disk (21 MB, loaded whole into the browser)
```

**The disk image is licensed Microsoft software, and the two ROMs are
IBM's.** Nothing of them is in this repository and nothing of them may be
committed (`build/` is ignored). Publish the page only where you are
allowed to.

## What it needs

The installation media and the release package that
[`tools/qemu/mkimage.sh`](../qemu/README.md) uses, in `$MEDIA` or next to
`tools/qemu/mkimage.sh`:

```
Microsoft Windows 3.1 (3.10.103) (3.5).7z
Microsoft MS-DOS 6.20 [German] (3.5).7z
Win_31.zip                         only its HIMEM.SYS is used
newshell-X.Y.Z.zip                 NewShell16 release (or --build)
```

and the tools of `mkimage.sh` plus `mkfs.fat` (Debian/Ubuntu
`qemu-system-x86 p7zip-full mtools fdisk dosfstools unzip curl git
python3`). PCjs and the ROMs are downloaded from
[the PCjs repository](https://github.com/jeffpar/pcjs) at a fixed commit
and cached in `~/.cache/newshell16/pcjs`. No KVM needed; a build takes
about a minute.

## How

1. `tools/qemu/mkimage.sh -d 4` installs MS-DOS and Windows 3.1 in QEMU,
   unattended (or `--image` takes an image it made earlier).
2. NewShell16 Setup runs silently in QEMU (`SETUP /S /T=RED95`) and makes
   NewShell16 the shell.
3. The FAT16 volume is copied onto a disk with the geometry of an IBM AT
   drive type (PCjs loads a raw image only with the geometry of the drive
   type it is configured with): MS-DOS's master boot code and boot sector
   code are kept, `IO.SYS` and `MSDOS.SYS` come first. `CONFIG.SYS` and
   `AUTOEXEC.BAT` are set for a 286 (`HIMEM.SYS`, `WIN /S`).
4. PCjs, its styles and the ROMs are copied next to `index.html`, which
   is made from `index.html.in`: the HTML that PCjs's `components.xsl`
   produces for the machine, written out so the browser needs no XSLT.

## Options

| Option | |
|---|---|
| `--out DIR` | output directory (default `build/pcjs`) |
| `--image IMG` | start from an image of `mkimage.sh -d 4` instead of making one |
| `--build` | `mkimage.sh -b`: build NewShell16 from source instead of a release package |
| `--theme NAME` | Retheme16 theme for Setup (`RED95`, `RED98`, ...) or `none` for the plain Windows 3.1 look |
| `--no-newshell` | keep Program Manager; the setup files stay in `C:\NSSETUP` |
| `--drive-type N` | IBM AT drive type (default 2: 20 MB; 3: 30 MB, 4: 62 MB, ...) |
| `--mhz N` | CPU clock (default 12); the Speed button under the screen runs faster |
| `--ram KB` | memory, 1024 to 15360 (default 4096) |
| `--pcjs-ref COMMIT` | PCjs commit to take PCjs and the ROMs from |

## Testing

`smoke.js` (Node with Playwright) serves the directory, boots the machine
in headless Chromium and saves screenshots of the desktop and of the Task
Manager (Ctrl+Esc):

```sh
node tools/pcjs/smoke.js build/pcjs /tmp/shots 45
```

## Colours and resolution

PCjs emulates the IBM VGA and nothing beyond it: no SVGA chip and no
VESA BIOS. Windows therefore runs with its VGA driver, 640x480 in 16
colours; 256 colours, 16 or 24 bit colour and 1024x768 are not
possible in PCjs. (The display drivers for those modes in QEMU, such as
vbesvga.drv of `mkimage.sh -d 16`, need a VESA BIOS with a linear frame
buffer and a 386 as well.) For high colour use `tools/qemu` instead.

The page draws the 640x480 screen scaled to fit (up to 1280x960); Full
Screen under the screen fills the display.

Click the screen to capture the mouse (the pointer is relative, as on a
real serial mouse; Esc releases it), then the keyboard goes to Windows
too. The disk lives in the browser's memory: changes are lost on reload.
