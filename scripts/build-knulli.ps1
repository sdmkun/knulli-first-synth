# Cross-compiles for Knulli (aarch64) in Docker and lays out the SD card files.
# Output: dist\ports\  ->  copy its contents to /userdata/roms/ports/ on the device.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$image = 'knulli-first-synth-build'

docker build -t $image (Join-Path $root 'docker')
if ($LASTEXITCODE -ne 0) { throw "docker build failed" }

$cmd = 'mkdir -p build/knulli && aarch64-linux-gnu-g++ -O2 -std=c++17 -Wall -Wextra ' +
       '-mcpu=cortex-a53 src/main.cpp src/synth.cpp ' +
       '-I/usr/include/SDL2 -I/usr/include/aarch64-linux-gnu/SDL2 -D_REENTRANT ' +
       '-L/usr/lib/aarch64-linux-gnu -lSDL2 -static-libgcc -static-libstdc++ ' +
       '-o build/knulli/ksynth'
docker run --rm -v "${root}:/src" $image sh -c $cmd
if ($LASTEXITCODE -ne 0) { throw "cross compile failed" }

$dist = Join-Path $root 'dist\ports'
$app = Join-Path $dist 'knulli-first-synth'
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item (Join-Path $root 'build\knulli\ksynth') $app -Force
Copy-Item (Join-Path $root 'package\KnulliFirstSynth.sh') $dist -Force
Copy-Item (Join-Path $root 'package\KnulliFirstSynth.sh.keys') $dist -Force

# Launchers must keep LF line endings or bash on the device chokes on '\r'.
foreach ($f in 'KnulliFirstSynth.sh', 'KnulliFirstSynth.sh.keys') {
    $p = Join-Path $dist $f
    $text = [IO.File]::ReadAllText($p) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($p, $text)
}
Write-Host "Ready: $dist  (copy contents to /userdata/roms/ports/)"
