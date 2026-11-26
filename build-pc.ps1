# Builds the PC tools and the SDL2 app with the local WinLibs GCC (no install, no PATH changes).
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$tools = Join-Path $root '..\_tools'
$gcc  = Join-Path $tools 'winlibs\mingw64\bin\gcc.exe'
$sdl  = Join-Path $tools 'sdl2\SDL2-2.32.10\x86_64-w64-mingw32'
New-Item -ItemType Directory -Force "$root\build" | Out-Null
$core = @("$root\src\level.c", "$root\src\atlas.c", "$root\src\objects.c", "$root\src\objtable.c", "$root\src\image.c")
foreach ($t in 'leveldump', 'levelpng', 'sheetpng') {
    & $gcc -O2 -Wall -o "$root\build\$t.exe" "$root\tools\$t.c" $core -static -lm
    if ($LASTEXITCODE -ne 0) { exit 1 }
    Write-Host "built build\$t.exe"
}
$app = $core + @("$root\src\gfx.c", "$root\src\gfx_sdl.c", "$root\src\config.c", "$root\src\imagewrite.c", "$root\src\game.c", "$root\src\bot.c", "$root\src\phystab.c", "$root\src\objchan.c", "$root\src\objchild.c", "$root\src\objz.c", "$root\src\anim.c", "$root\src\app.c")
& $gcc -O2 -Wall -Wno-unused-function -I"$sdl\include\SDL2" -o "$root\build\gd.exe" $app -L"$sdl\lib" -lmingw32 -lSDL2main -lSDL2 -lm -static-libgcc
if ($LASTEXITCODE -ne 0) { exit 1 }
Copy-Item "$sdl\bin\SDL2.dll" "$root\build\" -Force
Copy-Item "$tools\winlibs\mingw64\bin\libwinpthread-1.dll" "$root\build\" -Force
Write-Host "built build\gd.exe"



