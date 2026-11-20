#!/usr/bin/env bash
# Run the bundled bld-import-server on the web image's base, with only the
# packages scripts/ci/import-server-image-deps.txt lists, as the web server
# runs it (empty environment, a request per step):
#   scripts/ci/import-server-smoke.sh <bundle.tar.gz> [image]
set -euo pipefail
bundle=$(realpath "$1")
image=${2:-node:26.12.1-bookworm-slim}
src=$(cd "$(dirname "$0")/../.." && pwd)
docker run --rm -v "$bundle:/bundle.tar.gz:ro" -v "$src:/src:ro" "$image" bash -euc '
  deps=$(grep -v "^#" /src/scripts/ci/import-server-image-deps.txt | xargs)
  apt-get update -qq
  before=$(du -sx --block-size=1K /usr | cut -f1)
  apt-get install -y -qq --no-install-recommends $deps > /dev/null
  after=$(du -sx --block-size=1K /usr | cut -f1)
  mkdir -p /opt && tar -xzf /bundle.tar.gz -C /opt
  echo "bundle: $(du -sh /opt/bld-import-server | cut -f1); system packages: $(( (after - before) / 1024 )) MB"
  if ldd /opt/bld-import-server/bin/bld-import-server /opt/bld-import-server/plugins/*/*.so | grep "not found"; then exit 1; fi
  # BlueBrick'"'"'s sprites are GIFs: without the plugin every part read as 2 x 2 studs.
  test -f /opt/bld-import-server/plugins/imageformats/libqgif.so
  useradd -m app
  exe=/opt/bld-import-server/bin/bld-import-server
  su app -c "env -i $exe --version"
  su app -c "env -i $exe --sandbox-check" | tee /tmp/check.json
  grep -q "\"socketBlocked\":true" /tmp/check.json
  grep -q "\"forkBlocked\":true" /tmp/check.json
  job=$(su app -c "mktemp -d")
  su app -c "mkdir -p $job/in $job/out && cp /src/fixtures/bluebrick-oracle/tight-corner.mpd $job/in/"
  for step in analyse convert; do
    printf "{\"command\":\"%s\",\"input\":\"%s/in/tight-corner.mpd\",\"outDir\":\"%s/out\",\"libraries\":{\"parts\":[\"/src/parts/BlueBrickParts/parts\"]},\"options\":{\"mode\":\"part\",\"name\":\"Tight corner\"}}" \
      "$step" "$job" "$job" > /tmp/req.json
    chmod 644 /tmp/req.json
    su app -c "env -i $exe /tmp/req.json" | tee /tmp/out.json | head -c 400; echo
    grep -q "\"ok\":true" /tmp/out.json
  done
  ls -la $job/out $job/out/part
  # A 32 x 32 baseplate placed one by one keeps its GIF sprite'"'"'s size (a
  # converter that can'"'"'t read GIFs places it as a 2 x 2 stud placeholder).
  rm -rf $job/out && su app -c "mkdir -p $job/out && cp /src/fixtures/import-samples/baseplate.ldr $job/in/"
  printf "{\"command\":\"convert\",\"input\":\"%s/in/baseplate.ldr\",\"outDir\":\"%s/out\",\"libraries\":{\"parts\":[\"/src/parts/BlueBrickParts/parts\"]},\"options\":{\"mode\":\"parts\"}}" \
    "$job" "$job" > /tmp/req.json
  su app -c "env -i $exe /tmp/req.json" | tee /tmp/out.json | head -c 400; echo
  grep -q "\"ok\":true" /tmp/out.json
  tr -d "\n\r\t " < $job/out/layout.bbm | grep -o "<DisplayArea>[^/]*</X><Y>[^<]*</Y><Width>32</Width><Height>32</Height>" \
    || { echo "the 32 x 32 baseplate wasn'"'"'t placed at its own size: is Qt'"'"'s GIF plugin in the bundle?"; exit 1; }
'
