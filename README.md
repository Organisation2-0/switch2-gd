# switch2-gd

A fan-made, from-scratch engine that plays **Geometry Dash** levels, built for Switch 2
homebrew (libounce / kaiHEN) and for Windows. It contains **no game assets**: it reads the
sprites, fonts and levels from **your own copy** of Geometry Dash (Steam).

> Not affiliated with RobTop Games. You need a legal copy of Geometry Dash.
> Version **1.0** is a hobby-grade first release, see *Known issues* below.

## Features (v1.0)

- Loads the 22 official levels (`Resources/levels/N.txt`) and exported `.gdlvl` files.
- Sprite atlases (10 sheets, incl. fire / pixel / particle sheets), original bitmap font,
  GD-style menus (title, level select, pause, level complete).
- Game modes: cube, ship, ball, UFO, wave, robot, spider, mini size, gravity / speed portals,
  dual mode and the mirror portal.
- Pads and orbs (yellow, pink, blue, green, red), spikes, saws, blocks.
- Colour channels (copy, HSV, player colours, per-object default channels), colour triggers,
  multi-sprite objects (child sprites); **move / toggle / alpha / spawn** triggers with groups.
- Physics and hitboxes follow OpenGD (4 sub-steps per 60 Hz frame).
- Test bot: `gd.exe --bot all` (or `--bot 1,4,7-9`, `--botspeed N`) plays levels visibly;
  `gd.exe --selftest N` runs level N (0-based) headless. In the last full run all 22 levels
  were completed, some of them only with noclip fallback sections (see below).

## Known issues / not implemented

- **Not implemented:** rotate, pulse, follow and animate triggers; slopes (treated as full
  blocks); dash orbs; teleport portals; real robot / spider animations (drawn as cube);
  glow and particle effects; **audio**.
- Levels 12 (Theory of Everything) and 13 (Electroman Adventures) can not be passed by the bot
  without noclip, which hints at remaining physics / hitbox differences. Levels 14-22 were only
  verified with a mostly-cube bot run.
- Default colours of some decorations follow OpenGD's tables and may differ from the original.
- Physics and hitboxes were compared with OpenGD, not with the original game.
- **Switch 2:** the software-rendering path (SDL surface copied to the libnx framebuffer) has
  **not been verified on hardware** after the latest changes; memory use is about 85 MB for the
  sprite sheets. If it fails, check `sdmc:/switch/gd/log.txt` and open an issue.

## Build (Windows)

Needs MinGW-w64 GCC and the SDL2 development package; `build-pc.ps1` expects them in
`..\_tools\winlibs` and `..\_tools\sdl2`.

```powershell
.\build-pc.ps1
.\build\gd.exe                       # uses the default Steam path
.\build\gd.exe --res "D:\GD\Resources" --levels .\gd-levels
```

## Build and run (Switch 2)

Install the Switch 2 devkit (<https://github.com/ouncebrew/ounce-devkit>) plus `switch-sdl2`,
then `make`. A prebuilt `gd-switch2.nro` is attached to the release.

1. Run `.\prepare-sd.ps1 -Sd E:\` (optionally `-Steam "<path>"`, `-Levels <dir>`). It copies the
   app to `switch\gd-switch2.nro` and the needed files from your Steam install to
   `switch\gd\Resources`.
2. Start kaiHEN, open hbmenu (hold R while launching a game), run *Geometry Dash (fan port)*.

Controls: A / X / Y jump, B or + pause, D-pad / L / R choose a level.

## Layout

- `src/level.c` level decoding · `src/atlas.c`, `src/gfx.c` sheets and drawing
- `src/game.c` objects, physics, triggers · `src/bot.c` test bot · `src/app.c` menus, main loop
- `src/phystab.c`, `src/objchan.c`, `src/objchild.c` data tables derived from OpenGD
- `tools/` development helpers

## License

**GPL-3.0-or-later** (see `LICENSE`). Parts of the code are derived from
[OpenGD](https://github.com/Open-GD/OpenGD) (GPL-3.0). `vendor/stb_*.h` are public domain / MIT.
Geometry Dash and its assets belong to RobTop Games and are not included.
