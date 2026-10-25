# Testing in an emulator

`tools/emu/winemu.py` boots Windows 3.1 in [DOSBox-X](https://dosbox-x.com/)
with NewShell16 installed as the shell, drives it with keystrokes and takes
screenshots. It works headless (it starts `Xvfb` when no `DISPLAY` is set).

Requirements (Debian/Ubuntu): `dosbox-x xdotool imagemagick xvfb`.

You need an installed Windows directory, e.g. `Win/WINDOWS` from
`Win_31.zip` (see [reference-media.md](reference-media.md)). It is copied
into a scratch drive C: and the copy is modified, never the original.

```sh
wmake
tools/emu/winemu.py --windir ~/win31/Win/WINDOWS --work /tmp/nsemu --out shots \
    --set 'SYSTEM.INI:boot:taskman.exe=C:\NEWSHELL\NEWSHELL.EXE' \
    --script "sleep:22 shot:desktop.png key:ctrl+Escape sleep:2 shot:start.png"
```

- `--progman` keeps Program Manager as the shell (NewShell can then be
  started from it, or via `--set 'WIN.INI:windows:run=C:\NEWSHELL\NEWSHELL.EXE'`).
- `--set FILE:SECTION:KEY=VALUE` edits an INI file in the Windows
  directory before booting; an empty value removes the key. Changes persist
  in the `--work` directory.
- Script actions: `sleep:N`, `shot:FILE`, `key:KEYS` (xdotool names,
  comma separated), `type:TEXT` (`_` for spaces), `move:DX,DY`,
  `goto:X,Y`, `click:N`, `dclick:N`, `down:N`, `up:N`.

Setting `taskman.exe=` to NewShell makes Ctrl+Esc open NewShell's Task
Manager (or, with `--set 'NEWSHELL.INI:Taskbar:CtrlEscStart=1'`, the Start
menu), which allows driving most of the shell from the keyboard.

Mouse input is relative under DOSBox. With `MouseSpeed=0` in WIN.INI
(`--set 'WIN.INI:windows:MouseSpeed=0'`) one host pixel moves the Windows
pointer one pixel, and `goto:X,Y` first pins the pointer to the top-left
corner, so clicks and drags can be scripted at screen coordinates. Start
the script with `click:1` so DOSBox captures the mouse. Screenshots include
the DOSBox-X menu bar, so Windows' y = image y - 17.

Example: drag a file onto the Bin icon

```sh
--script "sleep:20 click:1 goto:75,149 click:1 down:1 move:-10,0 move:-20,-60 up:1"
```

## Testing with the Retheme16 theme engine

Build [Retheme16](https://github.com/ceisserer/win31_theme_engine)
(`make -f win16.mk O=/tmp/rt` builds `RETHEME.EXE` and `RETHEME.DLL`), then
install it on the test drive and start it through `load=`:

```sh
tools/emu/winemu.py --windir ~/win31/Win/WINDOWS --work /tmp/nsemu-rt --out shots \
    --install /tmp/rt/RETHEME.EXE=RETHEME/RETHEME.EXE \
    --install /tmp/rt/RETHEME.DLL=RETHEME/RETHEME.DLL \
    --install ~/win31_theme_engine/themes=RETHEME/THEMES \
    --set 'WIN.INI:windows:load=C:\RETHEME\RETHEME.EXE' --script "..."
```

`--install SRC=DEST` copies a file or directory to `DEST` on drive C:.
Run the same script with and without the engine and compare the
screenshots side by side.
