#!/usr/bin/env bash
# Renders resources/nekopad.svg into the PNGs and the Windows .ico the build compiles in.
# Needs rsvg-convert (librsvg) and ImageMagick.
set -euo pipefail
cd "$(dirname "$0")/../resources"
sizes=(16 24 32 48 64 128 256 512)
for s in "${sizes[@]}"; do
    rsvg-convert -w "$s" -h "$s" nekopad.svg -o "icons/app/nekopad_$s.png"
done
magick icons/app/nekopad_{16,24,32,48,64,128,256}.png nekopad.ico
