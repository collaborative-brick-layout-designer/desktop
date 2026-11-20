#!/usr/bin/env bash
# Bundle bld-import-server for the website's Docker image (docs/import-server.md):
#
#   packaging/import-server/bundle.sh <bld-import-server> <Qt root> <out.tar.gz>
#
# The tarball unpacks to bld-import-server/ with
#   bin/bld-import-server          RUNPATH $ORIGIN/../lib
#   lib/                           the Qt libraries it needs (and ICU)
#   plugins/platforms/libqoffscreen.so   the only platform plugin: headless
#   plugins/imageformats/          GIF and JPEG (BlueBrick's sprites are GIFs;
#                                  PNG is built into Qt Gui): without them every
#                                  part read as unknown size (2 x 2 studs)
#   bin/qt.conf                    so Qt finds plugins/ beside it
#   LICENSES/, THIRD-PARTY-NOTICES.txt
# libglvnd's GL dispatch libraries come along too (no driver: it never draws
# with GL). Other system libraries (glibc, fontconfig, freetype, glib, dbus,
# xkbcommon) come from the image; scripts/ci/import-server-image-deps.txt lists them.
# The archive is reproducible: sorted, fixed times and owner, gzip -n.
set -euo pipefail
exe=$1
qt=$2
out=$3
src=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root="$work/bld-import-server"
mkdir -p "$root/bin" "$root/lib" "$root/plugins/platforms" "$root/plugins/imageformats"
cp "$exe" "$root/bin/bld-import-server"
cp "$qt/plugins/platforms/libqoffscreen.so" "$root/plugins/platforms/"
# The image formats the parts libraries use (BlueBrickParts' sprites are GIFs,
# some custom parts JPEGs; PNG needs no plugin).
image_plugins="libqgif.so libqjpeg.so"
for p in $image_plugins; do cp "$qt/plugins/imageformats/$p" "$root/plugins/imageformats/"; done
printf '[Paths]\nPrefix=..\nPlugins=plugins\n' > "$root/bin/qt.conf"

# Every library under the Qt root that the program or the plugin loads.
copy_qt_deps() {
    ldd "$1" | awk '/=>/ { print $3 }' | while read -r lib; do
        case "$lib" in
            "$qt"/*)
                base=$(basename "$lib")
                [ -e "$root/lib/$base" ] && continue
                cp -L "$lib" "$root/lib/$base"
                copy_qt_deps "$lib"
                ;;
        esac
    done
}
copy_qt_deps "$exe"
copy_qt_deps "$qt/plugins/platforms/libqoffscreen.so"
for p in $image_plugins; do copy_qt_deps "$qt/plugins/imageformats/$p"; done
# libglvnd's dispatch libraries (no GL driver): Qt Gui links them, the
# converter never draws with GL (offscreen, raster), and without them the
# image would need Mesa (over 200 MB) just to start.
for l in "$root/bin/bld-import-server" "$root"/lib/*.so* "$root"/plugins/*/*.so; do
    ldd "$l" | awk '/=>/ { print $3 }'
done | sort -u | while read -r lib; do
    case "$(basename "$lib")" in
        libOpenGL.so.0|libGLX.so.0|libEGL.so.1|libGLdispatch.so.0|libGL.so.1)
            [ -e "$root/lib/$(basename "$lib")" ] || cp -L "$lib" "$root/lib/" ;;
    esac
done
strip --strip-unneeded "$root/bin/bld-import-server" "$root"/lib/*.so* "$root"/plugins/*/*.so 2>/dev/null || true
patchelf --set-rpath '$ORIGIN/../lib' "$root/bin/bld-import-server"
for p in "$root"/plugins/*/*.so; do patchelf --set-rpath '$ORIGIN/../../lib' "$p"; done
for l in "$root"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$l"; done

mkdir -p "$root/LICENSES"
cp "$src/LICENSE" "$root/LICENSES/LICENSE.txt"
[ -f "$src/THIRD-PARTY-NOTICES.txt" ] && cp "$src/THIRD-PARTY-NOTICES.txt" "$root/"

tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner -C "$work" -cf - bld-import-server \
    | gzip -n -9 > "$out"
sha256sum "$out" | awk '{ print $1 }' > "$out.sha256"
echo "bundled $(du -sh "$root" | cut -f1) into $out ($(du -h "$out" | cut -f1)), sha256 $(cat "$out.sha256")"
