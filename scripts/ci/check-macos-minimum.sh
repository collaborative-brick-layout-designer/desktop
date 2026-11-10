#!/usr/bin/env bash
# The app bundle's LSMinimumSystemVersion is at least what every binary in it
# needs (each Mach-O's LC_BUILD_VERSION minos, every architecture): otherwise
# Finder starts the app on an older macOS and dyld kills it without a word.
#
#   scripts/ci/check-macos-minimum.sh <path to .app>
set -euo pipefail
app=$1
plist="$app/Contents/Info.plist"
declared=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$plist" 2>/dev/null || true)
if [ -z "$declared" ]; then
    echo "::error::$plist has no LSMinimumSystemVersion"
    exit 1
fi
echo "LSMinimumSystemVersion: $declared"
newer() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ] && [ "$1" != "$2" ]; }
status=0
checked=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    checked=$((checked + 1))
    for minos in $(otool -l "$f" | awk '$1 == "minos" { print $2 }' | sort -u); do
        if newer "$minos" "$declared"; then
            echo "::error::${f#"$app"/} needs macOS $minos, newer than the declared $declared"
            status=1
        fi
    done
done < <(find "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/PlugIns" -type f -print0 2>/dev/null)
echo "Checked $checked Mach-O files"
[ "$checked" -gt 0 ] || { echo "::error::no Mach-O files found in $app"; exit 1; }
exit $status
