#!/bin/bash
# Knulli Ports launcher. Lives in /userdata/roms/ports/ next to knulli-first-synth/.
APPDIR="$(cd "$(dirname "$0")" && pwd)/knulli-first-synth"
cd "$APPDIR" || exit 1

BIN="./ksynth"
# Some SD card filesystems ignore the exec bit; fall back to a copy in /tmp.
chmod +x "$BIN" 2>/dev/null
if [ ! -x "$BIN" ]; then
    cp "$BIN" /tmp/ksynth && chmod +x /tmp/ksynth && BIN=/tmp/ksynth
fi

"$BIN" > "$APPDIR/log.txt" 2>&1
