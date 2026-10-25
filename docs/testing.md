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
  comma separated), `type:TEXT` (`_` for spaces), `move:DX,DY`, `click:N`,
  `dclick:N`.

Setting `taskman.exe=` to NewShell makes Ctrl+Esc open the Start menu,
which allows driving most of the shell from the keyboard. Mouse input is
relative under DOSBox and therefore not reliable for scripted tests.
