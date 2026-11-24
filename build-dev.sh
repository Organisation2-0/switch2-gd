#!/bin/bash
# Builds build/gdd.exe (so a running build/gd.exe is not in the way).
cd "$(dirname "$0")"
G="../_tools/winlibs/mingw64/bin/gcc.exe"
SDL="../_tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32"
"$G" -O2 -Wall -Wno-unused-function -I"$SDL/include/SDL2" -o build/gdd.exe src/level.c src/atlas.c src/objects.c src/objtable.c src/image.c src/gfx.c src/config.c src/imagewrite.c src/game.c src/bot.c src/phystab.c src/objchan.c src/objchild.c src/app.c -L"$SDL/lib" -lmingw32 -lSDL2main -lSDL2 -lm -static-libgcc 2>&1 | grep -i " error" 
true
