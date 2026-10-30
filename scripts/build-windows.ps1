# Builds a desktop test binary with MinGW g++ and the SDL2 MinGW dev package.
# Output: build\windows\ksynth.exe (+ SDL2.dll)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdlVersion = '2.30.12'
$sdl = Join-Path $root "deps\SDL2-$sdlVersion\x86_64-w64-mingw32"

if (-not (Test-Path $sdl)) {
    New-Item -ItemType Directory -Force (Join-Path $root 'deps') | Out-Null
    $tgz = Join-Path $root 'deps\sdl2-mingw.tar.gz'
    Invoke-WebRequest "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL2-devel-$sdlVersion-mingw.tar.gz" -OutFile $tgz
    tar -xzf $tgz -C (Join-Path $root 'deps')
}

$out = Join-Path $root 'build\windows'
New-Item -ItemType Directory -Force $out | Out-Null

& g++ -O2 -std=c++17 -Wall -Wextra `
    (Join-Path $root 'src\main.cpp') (Join-Path $root 'src\synth.cpp') `
    "-I$sdl\include\SDL2" "-L$sdl\lib" `
    -lmingw32 -lSDL2main -lSDL2 -static-libgcc -static-libstdc++ `
    -o (Join-Path $out 'ksynth.exe')
if ($LASTEXITCODE -ne 0) { throw "g++ failed" }

Copy-Item "$sdl\bin\SDL2.dll" $out -Force
Write-Host "Built $out\ksynth.exe"
