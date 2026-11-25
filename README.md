# switch2-gd

A fan-made, from-scratch engine that plays **Geometry Dash** levels, built for Switch 2
homebrew (libounce / kaiHEN) and for Windows. It contains **no game assets**: it reads the
sprites, fonts and levels from **your own copy** of Geometry Dash (Steam).

> Not affiliated with RobTop Games. You need a legal copy of Geometry Dash.
> Version **1.0.2** is a hobby-grade release, see *Known issues* below.

## Features

- Loads the 22 official levels (`Resources/levels/N.txt`) and exported `.gdlvl` files.
- Sprite atlases (10 sheets, incl. fire / pixel / particle sheets), original bitmap font,
  GD-style menus (title, level select, pause, level complete).
- Game modes: cube, ship, ball, UFO, wave, robot, spider, mini size, gravity / speed portals,
  dual mode and the mirror portal.
- Pads and orbs (yellow, pink, blue, green, red, black, dash), spikes, saws, blocks,
  breakable bricks, slopes, teleport portals.
- Colour channels (copy, HSV, player colours, light background, per-object default channels),
  multi-sprite objects (child sprites), glow, enter effects; **colour / pulse / move / rotate /
  stop / toggle / alpha / spawn** triggers with groups.
- Physics and hitboxes follow OpenGD (4 sub-steps per 60 Hz frame).
- Switch 2 renders on the GPU (OpenGL ES through OunceVK).
- Test bot: `gd.exe --bot all` (or `--bot 1,4,7-9`, `--botspeed N`) plays levels visibly;
  `gd.exe --selftest N` runs level N (0-based) headless. It retries from earlier checkpoints
  after a failure and only skips a stretch (noclip) when that fails many times.

## Test status

Bot, official levels: **1-15, 17, 18, 19 and 21 completed without skips** (12 and 19 needed
retries from checkpoints); **16 and 20 with one skipped stretch each**; **22 (Dash) not
completed**. These runs used builds from shortly before the last loader and drawing fixes;
with the 1.0.2 build, levels 1, 8, 14 and 21 were checked again.

## Known issues / not implemented

- **Dash** needs features that are missing: swing mode, gameplay rotation, force blocks,
  toggle orbs, time warp, shaders and camera triggers.
- **Not implemented:** follow and animate triggers, animated monsters, text objects, real robot /
  spider animations (drawn as cube), **audio**.
- Decorations that pulse with the music (rod balls, stars) differ in size from the original.
- Physics follow OpenGD plus own additions; they are not identical to the original game.
- **Switch 2:** memory use is about 85 MB for the sprite sheets. If the app fails to start,
  check `sdmc:/switch/gd/log.txt` (it also records the frame rate) and open an issue.

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
