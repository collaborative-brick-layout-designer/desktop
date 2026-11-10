#!/usr/bin/env bash
# A package holds the app and what it needs to run, nothing for building
# against: no headers, static or import libraries, CMake packages, qmake
# .pri files, nor installers of their own (vc_redist).
#
#   scripts/ci/check-package-files.sh <package folder>
set -euo pipefail
dir=$1
found=$(find "$dir" -type f \( -iname '*.h' -o -iname '*.hpp' -o -iname '*.a' -o -iname '*.lib' \
    -o -iname '*.cmake' -o -iname '*.pri' -o -iname 'vc_redist*.exe' \) | sort)
if [ -n "$found" ]; then
    echo "::error::Files for building, not running, are in the package:"
    echo "$found"
    exit 1
fi
echo "No developer files in $dir"
