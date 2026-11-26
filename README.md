# switch2-gd

A fan-made, from-scratch engine that plays **Geometry Dash** levels, built for Switch 2
homebrew (libounce / kaiHEN) and for Windows. It contains **no game assets**: it reads the
sprites, fonts and levels from **your own copy** of Geometry Dash (Steam).

> Not affiliated with RobTop Games. You need a legal copy of Geometry Dash.
> Version **1.2.0** is a hobby-grade release, see *Known issues* below.

## Features

- Loads the 22 official levels (`Resources/levels/N.txt`), exported `.gdlvl` files and the
  online levels saved in your own Geometry Dash (read only; those whose data the game keeps).
- Menus laid out like the original, reduced to what works here: title, level select, online
  levels with a page per level, garage with every icon of all eight forms and the original's
  colours, settings, key bindings, pause and level complete. Mouse, keyboard and controller.
- Music: the songs of the original for the official levels and the menus (read from your game),
  your own `.mp3` / `.ogg` / `.wav` files for custom levels; decorations pulse with the music.
  Sounds for starting and leaving a level and for crashing, fades, music and effect volume.
- Sprite atlases (10 sheets, incl. fire / pixel / particle sheets), original bitmap fonts,
  player icons loaded from the game's `Resources/icons`.
- Game modes: cube, ship, ball, UFO, wave, robot, spider, swing, mini size, gravity / speed
  portals, dual mode and the mirror portal.
- Pads and orbs (yellow, pink, blue, green, red, black, dash), spikes, saws, blocks,
  breakable bricks, slopes, teleport portals.
- Colour channels (copy, HSV, player colours, light background, per-object default channels),
  multi-sprite objects (child sprites), blending like the original (premultiplied alpha), glow,
  enter effects, rotating decorations, monsters, portal symbol tabs, ghost trail, camera shake,
  the animated effects of 2.2 (flames, energy balls, bursts started by animate triggers);
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

1.2.0: the test bot completes **all 22 official levels without a skipped stretch** (Windows
build, headless). Levels 12, 16, 19 and 22 need retries from checkpoints. The sound was checked
by recording the app's output and comparing it with the game's files and a video of the original.
Automated tests cover the Windows build only; sound on the console is untested by them.
`prepare-sd.ps1` copies more files than in earlier versions, so run it again when updating.

## Known issues / not implemented

- **2.2 is only partly supported.** Not implemented: camera triggers (zoom, offset, rotate;
  a static camera only lifts the flight borders), shaders, gradients, middleground, particle
  objects, area triggers, scale, keyframes, advanced follow, animations of monsters by trigger,
  other animated 2.1 / 2.2 objects (water, lava surfaces), player control and
  options triggers, platformer mode. Custom 2.2 levels that rely on these may look wrong or be
  impossible to finish.
- Some 2.2 rules were derived from the level data of Dash and not confirmed against the
  original game (teleport exit gravity and force, trigger channels, launch ramps).
- **Not implemented:** text objects, practice mode, coins and orbs, the monster attack
  animations except the first monster's bite, the glow of player icons, other sound effects
  (level complete, rewards ...), song and sound triggers inside levels.
- Online levels cannot be searched or downloaded; progress is not kept after closing the app.
  Very large levels (about 4 MB of level data) run slowly, about 18 frames per second on a PC.
- The pulsing of decorations (rod balls, orbs, circles) follows the loudness of the music in a
  simpler way than the original; without music it follows a steady beat.
- The level complete screen was not compared with the original.
- The look was compared with recordings of levels 1-21; level 22 (Dash) still differs clearly.
- Physics follow OpenGD plus own additions; they are not identical to the original game.
- The garage shows `name=` from the config file (default "Player").
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

The *Online Levels* menu (title screen, button right of *Play*) lists the `.gdlvl` files of the
levels folder and the levels saved in `%LOCALAPPDATA%\GeometryDash\CCGameManager.dat`
(`--save <file>` picks another one, `--save none` leaves them out).

**Level and song in one file (`.aiogdl`):** `build\aiopack.exe` packs a level (`.gdlvl`, `.gmd`
or a level string) and an MP3 / Ogg / WAV file together. Start it without anything and it asks for
the level, the song and the place to save; or drag both files onto it; or use it in a terminal:

```powershell
.\build\aiopack.exe level.gdlvl song.mp3            # writes level.aiogdl
.\build\aiopack.exe --info level.aiogdl
.\build\aiopack.exe --unpack level.aiogdl .\out
```

Put the `.aiogdl` into the levels folder like a `.gdlvl`; its song plays without choosing
anything. Levels saved in your game can be turned into `.gdlvl` files with
`gd.exe --export-saved <folder>`. The format is described in `src/aio.h`. A bundle contains the
song: only pass on files whose music you are allowed to share.

**Own songs:** put them into the folder `gd-music` next to where you start the app (it is created
when the song list is first opened; `--music <dir>` picks another one; on the Switch
`switch\gd\music`). Then open a level in *Online Levels*, press *Change* (Y on the controller, M on
the keyboard) and choose a file; official songs can be chosen too. The choice is kept in
`gd-songs.ini`. `music=0` together with `sfx=0` in the config file keeps the sound device closed.

## Build and run (Switch 2)

Install the Switch 2 devkit (<https://github.com/ouncebrew/ounce-devkit>) plus `switch-sdl2`,
then `make`. A prebuilt `gd-switch2.nro` is attached to the release.

1. Run `.\prepare-sd.ps1 -Sd E:\` (optionally `-Steam "<path>"`, `-Levels <dir>` with `.gdlvl` /
   `.aiogdl` files, `-Music <dir>` with your own songs). It copies the
   app to `switch\gd-switch2.nro`, the needed files from your Steam install to
   `switch\gd\Resources` and your saved online levels to `switch\gd\levels` (this needs a
   built or downloaded `gd.exe`; `-NoSavedLevels` leaves them out).
2. Start kaiHEN, open hbmenu (hold R while launching a game), run *Geometry Dash (fan port)*.

Controls: A / X / Y jump, B or + pause. Menus: D-pad moves, A chooses, B goes back; L / R turn
pages (level select, garage), ZL / ZR change the form in the garage. On the title screen X opens
the garage and Y the settings (PC: G and O).

## Layout

- `src/level.c` level decoding · `src/atlas.c`, `src/gfx.c` sheets and drawing
- `src/game.c` objects, physics, triggers · `src/bot.c` test bot · `src/app.c` levels, main loop
- `src/menus.c` screens and input · `src/saves.c` level information and saved levels
- `src/audio.c` music playback · `src/songs.c` which song belongs to a level
- `src/aio.c` the `.aiogdl` format · `tools/aiopack.c` the packing tool
- `src/phystab.c`, `src/objchan.c`, `src/objchild.c` data tables derived from OpenGD
- `tools/` development helpers

## License

**GPL-3.0-or-later** (see `LICENSE`). Parts of the code are derived from
[OpenGD](https://github.com/Open-GD/OpenGD) (GPL-3.0). The libraries in `vendor/` (stb,
dr_mp3) are public domain / MIT.
Geometry Dash and its assets belong to RobTop Games and are not included.
