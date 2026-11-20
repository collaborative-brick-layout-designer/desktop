Open samples for the website's import (docs/import-server.md), e.g. to replay
uploads through a firewall: built only from LDraw.org's freely licensed parts.

- `two-bricks.lxfml`: an LDD model, two 2 x 4 bricks (LDD design 3001).
- `two-straights.ldr`, `station.mpd`: tiny LDraw models (and the LDraw import fuzzer's seeds).
- `baseplate.ldr`: a 32 x 32 baseplate and a brick; placed one by one, the
  baseplate takes its GIF sprite's 32 x 32 studs (the converter bundle's
  smoke test: without Qt's GIF plugin it would get a 2 x 2 placeholder).
- `../studio/zipcrypto-small.io`: a Studio file, encrypted like Studio's, with
  a submodel and its own custom part (scripts/make-zipcrypto-io.py).
- `../bluebrick-oracle/tight-corner.ldr` and `.mpd`: LDraw layouts (300 parts).
