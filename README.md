# Ounce Dash

**Ounce Dash** is a fan-made, from-scratch engine that plays **Geometry Dash** levels, built for
Switch 2 homebrew (libounce / kaiHEN) and for Windows. It contains **no game assets**: it reads the
sprites, fonts and levels from **your own copy** of Geometry Dash (Steam).

> Not affiliated with RobTop Games. You need a legal copy of Geometry Dash.
>
> **Version 1.0.0** is the first full release (before, the project was called *switch2-gd*; those
> releases are the 0.x versions). It is still a hobby project, see *Known issues* below.

## Download

From the [latest release](https://github.com/Organisation2-0/ounce-dash/releases/latest):

- **Windows:** `ounce-dash-1.0.0-windows.zip`. Unpack it and start `ounce-dash.exe`; it finds Geometry
  Dash in the default Steam folder (otherwise see *Build and run (Windows)*).
- **Switch 2:** `ounce-dash.nro`. Put it next to `prepare-sd.ps1` from this repository and follow
  *Build and run (Switch 2)*.

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
- Practice mode (button in the pause menu): checkpoints set automatically or with Z / ZR and
  removed with X / ZL, practice music, its own progress bar. The restart button starts the level
  from the beginning.
- Pads and orbs (yellow, pink, blue, green, red, black, dash), spikes, saws, blocks,
  breakable bricks, slopes (the icon lies flat on them and slides down as well as up), teleport
  portals. Dash orbs draw the flame, the burst and the spinning cube; after orbs and pads the icon
  leaves its trail until it lands.
- Coins: secret and user coins are picked up by touching them. They are kept (in `ounce-dash-coins.ini`
  next to the config file) once the level is finished in normal mode, show in the pause, level
  complete and level screens, and appear as outlines in the level afterwards.
- Colour channels (copy, HSV, player colours, light background, per-object default channels),
  multi-sprite objects (child sprites), blending like the original (premultiplied alpha), glow,
  enter effects, rotating decorations, monsters, portal symbol tabs, ghost trail, camera shake,
  the animated effects of 2.2 (flames, energy balls, bursts started by animate triggers);
  **colour / pulse / move / rotate / stop / toggle / alpha / spawn** triggers with groups.
- 2.2 gameplay: rotated and reversed gameplay (with trigger channels), free mode, spider /
  toggle / teleport orbs, teleport portals and trigger (exit gravity and force), gravity toggle
  portal, force blocks, the helper blocks, passable blocks, damage squares and circles.
- The 2.2 triggers, see *2.2 triggers* below.
- Levels start the way their settings say: any of the eight forms, mini, speed, dual, flipped
  gravity, mirrored and at the height of a spawn group (start position objects are not used yet).
- Hitboxes as in Geometry Dash 2.2: the icon has an outer box (spikes, portals, pads, orbs, landing
  on blocks; it never turns) and a small inner box in its middle that makes running into a block or
  slope deadly. Spikes turned by odd angles have turned boxes, saws round ones, "no touch" objects
  collide with nothing. See *Hitboxes*.
- Movement follows OpenGD (4 sub-steps per 60 Hz frame).
- Renders on the GPU: OpenGL ES through OunceVK on the Switch 2, OpenGL 2.1 on Windows.
- Test bot: `ounce-dash.exe --bot all` (or `--bot 1,4,7-9`, `--botspeed N`) plays levels visibly;
  `ounce-dash.exe --selftest N` runs level N (0-based) headless. It retries from earlier checkpoints
  after a failure and only skips a stretch (noclip) when that fails many times.

## 2.2 triggers

| Area | Triggers |
|---|---|
| Groups | move (also to a target or in the direction of one), rotate, scale, follow, follow player Y, advanced follow with edit and retarget, keyframes, stop / pause / resume, toggle, alpha, spawn (delay, random delay, order, group remapping), easing as in the original |
| Colour and picture | colour, pulse, gradients (four blend modes), background / ground / middleground change and speed, enter effects and enter areas, area move / rotate / scale / fade / tint with edit and stop |
| Shaders | all of the editor's: shock wave and line, glitch, chromatic and chromatic glitch, pixelate, lens circle, radial and motion blur, bulge, pinch, grey scale, sepia, invert, hue, colour change, split screen |
| Camera | zoom, static, offset, gameplay offset, rotate, edge, shake, user interface objects |
| Logic | pickup, count, instant count, item edit and compare, persistent items, timers and time events, events, collision and instant collision, collision / state / toggle blocks, touch, random, advanced random, sequence, reset, on death |
| Player | gravity, time warp, reverse, rotate gameplay, teleport, stop jump, hide / show, options (hide ground, middleground, icons or attempts, no death sound, respawn time), end |
| Sound | sound effect and its edit, song and its edit |
| Objects | particle objects and spawn particle |

**How far to trust them:** the game does not document what these triggers do. Which setting means
what was taken from community lists of the level format, and the behaviour behind it is this
project's own reading. Most were only checked with small test levels made for it, not against the
original; expect differences in custom levels, above all with areas, shaders, particles,
advanced follow, keyframes, the user interface and sounds.

The **shaders** are the exception: how each setting turns into the picture, and which layers the
shader setup trigger lets them reach, was worked out from Geometry Dash 2.2081 itself (the code is
this project's own). They were still not compared side by side with the original except for the
sepia and lens scenes at the start of Dash. Left out: motion blur bound to a moving object, and the
option to blur only empty places.

Sound and song triggers need the files: effects come from the game (`Resources/sfx` and the ones
it has downloaded), other songs from the game's save folder or your music folder.

## Hitboxes

| Icon | Outer box (normal / mini) | Inner box |
|---|---|---|
| Cube, ship, ball, UFO, robot, swing | 30 / 18 | 9 |
| Spider | 27 / 16.2 | 8.1 |
| Wave | 10 / 6 | 3 |

(Level units, one block is 30.) The inner box keeps its size for the mini icon. Type and size of
the objects are in `src/phystab.c`, generated by `tools/genphys.js` from the object table of
[gd-levelgen](https://github.com/kholkink/gd-levelgen) (MIT), which was read out of Geometry Dash
2.2081; its description of how blocks catch the icon is followed too. Slopes still use this
project's own, simpler rules, and teleport portals fire later than the table's box would (with it,
Deadlocked ends in spikes behind an exit).

The invisible helper blocks work: H (hitting the head is harmless), F (hitting the head turns
gravity around), D (the wave slides on blocks), S (ends a dash) and J (a click is not kept for the
next orb).

## Graphics settings

*Settings → Graphics* (title screen). A or a click changes a row, left / right or a right click
step through its values. The choices are kept in the config file.

| Row | Meaning |
|---|---|
| Resolution | PC: window size. Switch 2: 720p to 4K picture, used from the next start. The textures stay the same, only edges get smoother. |
| Fullscreen | PC only. |
| VSync | Off: the newest picture is shown instead of waiting in a queue. Less input delay, more power. |
| Frame Rate | Switch 2 only: 60 or 120 FPS. 120 needs a screen that can do 120 Hz. |
| FPS Counter | Lower left corner; on the Switch 2 also the rate pictures are presented at. |
| Glow, Particles, Screen Shake | Effects on or off. |

If the app no longer starts after a change on the console, delete `switch\ounce-dash\config.ini` (this also
resets icons, colours and key bindings) or set `height=720`, `vsync=1`, `fps=60` in it.

## Test status

1.0.0: the test bot completes **all 22 official levels without a skipped stretch** (Windows build,
headless). Levels 12, 16 and 19 need retries from checkpoints. A bot success means the levels can
be finished, not that the physics match the original.

Automated tests cover the Windows build only. **Not tested on a console** in this version: the
shaders, the dash effects, sliding on slopes, coins, VSync off, 120 FPS, the higher resolutions,
touch and the new sounds. Full screen shader effects cost graphics time; how much on the console is
not known.

`prepare-sd.ps1` copies more files than in earlier versions: run it again when updating.

## Known issues / not implemented

- **2.2 is not complete.** Not implemented: platformer mode with its checkpoints and the player
  control it needs (only *stop jump* works), taking the controls away by the options trigger (left
  out on purpose, in an earlier test the end of Dash could not be survived with it; not tried again
  with the new hitboxes), camera mode apart from free mode, link
  visible, background effect on / off, the editor-only guides, text objects, animations of monsters
  by trigger, animated water and lava surfaces. Levels that rely on these may look wrong or be
  impossible to finish.
- The camera triggers, gradients and shaders were compared with the recording of Dash only in a few
  places. Where the alignment of user interface objects is concerned, the rules are guesses.
- Some 2.2 rules were derived from the level data of Dash and not confirmed against the
  original game (teleport exit gravity and force, trigger channels, launch ramps).
- **Not implemented:** the orbs (currency) of the original, the monster attack animations except the first monster's
  bite, the glow of player icons, other sound effects of the menus (level complete, rewards ...).
- Online levels cannot be searched or downloaded; apart from coins, progress is not kept after closing the app.
  Very large levels (about 4 MB of level data) run slowly, about 18 frames per second on a PC.
- The pulsing of decorations (rod balls, orbs, circles) follows the loudness of the music in a
  simpler way than the original; without music it follows a steady beat.
- The level complete screen was not compared with the original.
- The look was compared with recordings of levels 1-21; level 22 (Dash) still differs in places.
- Physics follow OpenGD plus own additions; they are not identical to the original game. Some
  strengths of pads and orbs differ in particular for ship, UFO and ball.
- Start position objects are ignored: a level that has them still starts at its beginning.
- The garage shows `name=` from the config file (default "Player").
- **Switch 2:** memory use is about 85 MB for the sprite sheets. If the app fails to start,
  check `sdmc:/switch/ounce-dash/log.txt` (it also records the frame rate) and open an issue.

## Build and run (Windows)

Needs MinGW-w64 GCC and the SDL2 development package; `build-pc.ps1` expects them in
`..\_tools\winlibs` and `..\_tools\sdl2`. Running it needs a graphics driver with OpenGL 2.1.

```powershell
.\build-pc.ps1
.\build\ounce-dash.exe                       # uses the default Steam path
.\build\ounce-dash.exe --res "D:\GD\Resources" --levels .\ounce-dash-levels
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
`ounce-dash.exe --export-saved <folder>`. The format is described in `src/aio.h`. A bundle contains the
song: only pass on files whose music you are allowed to share.

**Own songs:** put them into the folder `ounce-dash-music` next to where you start the app (it is created
when the song list is first opened; `--music <dir>` picks another one; on the Switch
`switch\ounce-dash\music`). Then open a level in *Online Levels*, press *Change* (Y on the controller, M on
the keyboard) and choose a file; official songs can be chosen too. The choice is kept in
`ounce-dash-songs.ini`. `music=0` together with `sfx=0` in the config file keeps the sound device closed.

## Build and run (Switch 2)

Install the Switch 2 devkit (<https://github.com/ouncebrew/ounce-devkit>) plus `switch-sdl2`,
then `make`. A prebuilt `ounce-dash.nro` is attached to the release.

1. Run `.\prepare-sd.ps1 -Sd E:\` (optionally `-Steam "<path>"`, `-Levels <dir>` with `.gdlvl` /
   `.aiogdl` files, `-Music <dir>` with your own songs). It copies the
   app to `switch\ounce-dash.nro`, the needed files from your Steam install to
   `switch\ounce-dash\Resources` and your saved online levels to `switch\ounce-dash\levels` (this
   needs a built or downloaded `ounce-dash.exe`; `-NoSavedLevels` leaves them out).
2. Start kaiHEN, open hbmenu (hold R while launching a game), run *Ounce Dash*.

**Coming from switch2-gd:** the app's folder and file names changed. `prepare-sd.ps1` copies your
settings, songs, coins and levels from `switch\gd` to `switch\ounce-dash`; the old folder and
`switch\gd-switch2.nro` can then be deleted. On Windows the old files (`gd-config.ini`,
`gd-levels`, `gd-music`, `gd-songs.ini`) are still used as long as there are no new ones.

Controls: A / X / Y jump, B or + pause. Menus: D-pad moves, A chooses, B goes back; L / R turn
pages (level select, garage), ZL / ZR change the form in the garage. On the title screen X opens
the garage and Y the settings (PC: G and O).

Touch screen (handheld): touch and hold to jump, the button in the upper right corner pauses. In the
menus tap a button, drag the level and song lists, swipe sideways in the level select and the garage,
drag the volume sliders. Touch is **untested on a console**; `log.txt` notes the first position.

## Layout

- `src/level.c` level decoding · `src/atlas.c`, `src/gfx.c`, `src/gfx_gl.c` sheets and drawing
- `src/game.c` objects and physics · `src/bot.c` test bot · `src/app.c` levels, main loop
- `src/triggers.c` triggers · `src/motion.c` keyframes, advanced follow, areas ·
  `src/visual.c` particles, shaders, middleground, user interface and sound triggers
- `src/menus.c` screens and input · `src/saves.c` level information and saved levels
- `src/audio.c` music playback · `src/songs.c` which song belongs to a level ·
  `src/coins.c` owned coins · `src/practice.c` practice mode
- `src/aio.c` the `.aiogdl` format · `tools/aiopack.c` the packing tool
- `src/objchan.c`, `src/objchild.c` data tables derived from OpenGD · `src/phystab.c` hitboxes
  (`tools/genphys.js`)
- `tools/` development helpers

## License

**GPL-3.0-or-later** (see `LICENSE`). Parts of the code are derived from
[OpenGD](https://github.com/Open-GD/OpenGD) (GPL-3.0). The object hitbox data and the block
collision rules come from [gd-levelgen](https://github.com/kholkink/gd-levelgen), MIT License,
Copyright (c) 2026 NoJokes (`vendor/gd-levelgen-LICENSE.txt`). The libraries in `vendor/` (stb,
dr_mp3) are public domain / MIT.
Geometry Dash and its assets belong to RobTop Games and are not included.
