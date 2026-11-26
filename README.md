# switch2-gd

A fan-made, from-scratch engine that plays **Geometry Dash** levels, built for Switch 2
homebrew (libounce / kaiHEN) and for Windows. It contains **no game assets**: it reads the
sprites, fonts and levels from **your own copy** of Geometry Dash (Steam).

> Not affiliated with RobTop Games. You need a legal copy of Geometry Dash.
> Version **1.1.0** is a hobby-grade release, see *Known issues* below.

## Features

- Loads the 22 official levels (`Resources/levels/N.txt`) and exported `.gdlvl` files.
- Sprite atlases (10 sheets, incl. fire / pixel / particle sheets), original bitmap fonts;
  title, level select and pause screen laid out like the original, plus a level complete screen.
- Game modes: cube, ship, ball, UFO, wave, robot, spider, swing, mini size, gravity / speed
  portals, dual mode and the mirror portal.
- Pads and orbs (yellow, pink, blue, green, red, black, dash), spikes, saws, blocks,
  breakable bricks, slopes, teleport portals.
- Colour channels (copy, HSV, player colours, light background, per-object default channels),
  multi-sprite objects (child sprites), blending like the original (premultiplied alpha), glow,
  enter effects, rotating decorations, monsters, portal symbol tabs, ghost trail, camera shake;
  **colour / pulse / move / rotate / stop / toggle / alpha / spawn** triggers with groups.
- 2.2 gameplay: rotated and reversed gameplay (with trigger channels), free mode, spider /
  toggle / teleport orbs, teleport portals and trigger (exit gravity and force), force blocks,
  H blocks, passable blocks, time warp, gravity, follow and end triggers, pickup / count /
  instant count / collision / touch / random / sequence triggers.
- Physics and hitboxes follow OpenGD (4 sub-steps per 60 Hz frame).
- Switch 2 renders on the GPU (OpenGL ES through OunceVK).
- Test bot: `gd.exe --bot all` (or `--bot 1,4,7-9`, `--botspeed N`) plays levels visibly;
  `gd.exe --selftest N` runs level N (0-based) headless. It retries from earlier checkpoints
  after a failure and only skips a stretch (noclip) when that fails many times.

## Test status

1.1.0: the test bot completes **all 22 official levels without a skipped stretch** (Windows
build, headless). Levels 12, 16, 19 and 22 need retries from checkpoints. The look of levels
1-21 and of the title, level select and pause screens was compared with recordings of the
original on spot samples. The Switch build compiles but this version has not been tried on the
console. `prepare-sd.ps1` copies more files than in 1.0.x, so run it again when updating.

## Known issues / not implemented

- **2.2 is only partly supported.** Not implemented: camera triggers (zoom, offset, rotate;
  a static camera only lifts the flight borders), shaders, gradients, middleground, particle
  objects, area triggers, scale, keyframes, advanced follow, animate, player control and
  options triggers, platformer mode. Custom 2.2 levels that rely on these may look wrong or be
  impossible to finish.
- Some 2.2 rules were derived from the level data of Dash and not confirmed against the
  original game (teleport exit gravity and force, trigger channels, launch ramps).
- **Not implemented:** text objects, practice mode, orb counters and collected coins in the
  menus, the monster attack animations except the first monster's bite, **audio**. The volume
  sliders, social buttons and several menu buttons are drawn but do nothing.
- Decorations that pulse with the music (rod balls, orbs, circles) follow a steady beat instead.
- The garage and the key settings screens do not look like the original.
- The look was compared with recordings of levels 1-21; level 22 (Dash) still differs clearly.
- Physics follow OpenGD plus own additions; they are not identical to the original game.
- The title screen shows `name=` from the config file (default "Player").
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

Controls: A / X / Y jump, B or + pause, D-pad / L / R choose a level. On the title screen X opens
the garage and Y the key settings (PC: G and O).

## Layout

- `src/level.c` level decoding · `src/atlas.c`, `src/gfx.c` sheets and drawing
- `src/game.c` objects, physics, triggers · `src/bot.c` test bot · `src/app.c` menus, main loop
- `src/phystab.c`, `src/objchan.c`, `src/objchild.c` data tables derived from OpenGD
- `tools/` development helpers

## License

**GPL-3.0-or-later** (see `LICENSE`). Parts of the code are derived from
[OpenGD](https://github.com/Open-GD/OpenGD) (GPL-3.0). `vendor/stb_*.h` are public domain / MIT.
Geometry Dash and its assets belong to RobTop Games and are not included.
