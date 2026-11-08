#!/usr/bin/env bash
# Renders every app icon file from logo.svg, the one source: a 32 x 32
# viewBox on a 4-unit stud grid, so brick edges land on whole pixels at 16,
# 24, 32, 48 px and every larger multiple of 16.
# Needs rsvg-convert (librsvg) and magick (ImageMagick 7).
# Usage: packaging/icons/source/build-icons.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
icons="$(cd "$here/.." && pwd)"
src="$here/logo.svg"
name=brick-layout-designer
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

png() { rsvg-convert -w "$1" -h "$1" "$src" -o "$2"; }

# Every size, each rendered at exactly its own size (never scaled down from
# a bigger PNG). The hicolor theme, the window icon (Qt resource) and the
# ICO use these; 256 px is brick-layout-designer.png itself.
mkdir -p "$icons/png"
for s in 16 24 32 48 64 128 512; do png "$s" "$icons/png/$name-$s.png"; done
png 256 "$icons/$name.png"
png 1024 "$tmp/1024.png"

byte() { printf "\\x$(printf %02x $(($1 & 255)))"; }
be32() { byte $(($1 >> 24)); byte $(($1 >> 16)); byte $(($1 >> 8)); byte "$1"; }
le16() { byte "$1"; byte $(($1 >> 8)); }
le32() { le16 $(($1 & 65535)); le16 $(($1 >> 16)); }
size_of() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1"; }

# Windows: the .exe icon, an ICO of PNG entries (Vista and later), as
# compact as the PNGs themselves.
ico=()
for s in 16 24 32 48 64 128; do ico+=("$s:$icons/png/$name-$s.png"); done
ico+=("256:$icons/$name.png")
{
    le16 0; le16 1; le16 ${#ico[@]}
    offset=$(( 6 + 16 * ${#ico[@]} ))
    for e in "${ico[@]}"; do
        s="${e%%:*}" f="${e#*:}" n=$(size_of "${e#*:}")
        byte $(( s % 256 )); byte $(( s % 256 )); byte 0; byte 0; le16 1; le16 32; le32 "$n"; le32 "$offset"
        offset=$(( offset + n ))
    done
    for e in "${ico[@]}"; do cat "${e#*:}"; done
} > "$icons/$name.ico"

# macOS: an .icns of PNG entries (type, then big-endian length with the
# 8-byte header, then the PNG), the same layout iconutil writes.
body="$tmp/body"
: > "$body"
for entry in icp4:png/$name-16.png icp5:png/$name-32.png ic11:png/$name-32.png \
             ic12:png/$name-64.png ic07:png/$name-128.png ic08:$name.png ic13:$name.png \
             ic09:png/$name-512.png ic14:png/$name-512.png ic10:@1024; do
    type="${entry%%:*}" file="${entry#*:}"
    [ "$file" = @1024 ] && file="$tmp/1024.png" || file="$icons/$file"
    printf %s "$type" >> "$body"
    be32 $(( $(size_of "$file") + 8 )) >> "$body"
    cat "$file" >> "$body"
done
{ printf icns; be32 $(( $(size_of "$body") + 8 )); cat "$body"; } > "$icons/$name.icns"
echo "icons written to $icons"
