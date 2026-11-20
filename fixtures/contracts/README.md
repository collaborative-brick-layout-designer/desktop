# Web/desktop contracts

The same files live in `fixtures/contracts/` in both repos (desktop:
brick-layout-designer, web: collaborative-brick-layout-designer). Tests in
both read them, and both check every file against `MANIFEST.sha256`, so a
change in one repo fails until the other has the same copy.

- `imported-part.xml`: what the desktop's ImportToPart writes for a 12 x 12
  stud L with a 2 x 2 hole (its import date replaced by `(date)`).
- `imported-part.json`: what that XML reads as in both apps (connections,
  SnapMargin, PickShape, Designer, ImportedFrom), what the server must
  never store or show (`neverOnTheServer`), and hit points both apps' pick
  tests must agree on.
- `sidecar-hash.json`: the sidecarItems blob hash of each JSON text.
- `update-guard.json`: updates and state vectors (hex) and whether the
  update guard refuses them (web apps/server/src/ws/updateGuard.ts, desktop
  src/sync/UpdateGuard.cpp), with the limits both use; `gridSize` (a
  grid's cell size) and `drawingSettings` (every number a sheet is drawn
  from, with its limits) are what the server's grid guard refuses
  (apps/server/src/ws/gridGuard.ts, drawingSettings.ts).
- `doc-shape.json`: doc shape 2 (packages/ydoc shape.ts, desktop
  sync/DocShape.cpp): the drawing order by z, the z a reorder sets, and
  concurrent edits (a web update made on a base doc, and the edit the
  desktop makes at the same time) with what the doc must hold after.
  The web's `BLD_WRITE_CONTRACTS=1 vitest run src/editor/test/docShape.test.ts`
  writes the concurrent ones.
- `module-members.json`: a module's parts one key per part (doc shape 3;
  packages/ydoc moduleMembers.ts, desktop sync/ModuleMembers.cpp): the
  keys, the values a key may hold, what a starting list and its keys read
  as, and the keys a writer writes for a wanted list; and `docs`: a layout
  from before the keys, edits the web made on it with the edit the desktop
  makes at the same time and what the module must hold after, and a layout
  with keys. The web's
  `BLD_WRITE_CONTRACTS=1 vitest run src/editor/test/moduleMembersTogether.test.ts`
  writes `docs`.
- `shared-module-placement.json`: where a library version of a module sits
  in a layout (docs/shared-modules.md; web packages/modules placement.ts,
  desktop src/edit/SharedModulePlacement.cpp): a version's parts placed by
  a placement, the placement after a move, a turn or a drop, the placement
  worked out from the parts, and whether placed parts are still the
  version's. The web's
  `BLD_WRITE_CONTRACTS=1 pnpm --filter @cld/modules test` writes the
  `expect` values.
- `shared-module-keep.json`: updates that keep the ids of a shared module's
  unchanged parts (docs/shared-modules.md, phase 3; web packages/modules
  keepIds.ts, desktop src/edit/SharedModuleKeep.cpp): which parts are
  matched, which go, which come in new, each sheet's drawing order after,
  and the new parts' sets. The web's
  `BLD_WRITE_CONTRACTS=1 pnpm --filter @cld/modules test` writes the
  `expect` values.
- `shared-module-settle.json`: finishing an undo or redo for a shared module
  after such an update (doc shape 5; web packages/modules settle.ts,
  desktop src/edit/SharedSettle.cpp): what goes, and what moves where; and
  `retired`, the parts an update took out that a step brought back. The
  same command writes its `expect` values.
- `part-naming.json`: a server custom part, placed on the website as
  `custom:<id>`, which every export names by its part number and the
  desktop's parts cache finds by either.

- `track-kinds.json`: which connection types are track and what kind
  (rail, monorail, Duplo, ...), with each kind's defaults (trains design
  2.1; web packages/parts-catalog/src/track/tables.ts, desktop
  src/track/data/track-kinds.json).
- `track-paths.json`: the ways through parts where the rules would be wrong
  (web tables.ts, desktop src/track/data/track-paths.json).
- `track-network.json`: each part's ways through and switches, and a small
  layout's network: switches, partners, exits, where each connection is and
  each way's length (web packages/parts-catalog/src/track/partTrack.ts and
  network.ts, desktop src/track/PartTrack.cpp and TrackNetwork.cpp). The
  web's `BLD_WRITE_CONTRACTS=1 pnpm --filter @cld/parts-catalog test`
  writes `ports` and `lengths`.
- `route-search.json`: route search on a small made-up network (trains
  design 4): the way found, its length, the switches it needs and where it
  turns the train round, or why there is none; and each route checked again
  with a part taken out (web packages/parts-catalog/src/track/routes.ts,
  desktop src/track/Routes.cpp). The web's
  `BLD_WRITE_CONTRACTS=1 pnpm --filter @cld/parts-catalog test` writes the
  `expect` and `status` values.

To change them: edit (or, for the imported part, run the desktop's
`bld_sync_tests --gtest_filter='Contracts.*'` with `BLD_WRITE_CONTRACTS=1`),
then `sha256sum doc-shape.json imported-part.json imported-part.xml module-members.json module-setup.json part-naming.json presence-colors.json route-search.json shared-module-keep.json shared-module-placement.json shared-module-settle.json sidecar-hash.json track-kinds.json track-network.json track-paths.json update-guard.json > MANIFEST.sha256`
and copy the folder to the other repo.
- `presence-colors.json`: each person's first-choice presence color on a
  layout, and the colors a group of people here get (web
  apps/web/src/editor/presenceColors.ts, desktop src/sync/Presence.cpp).
- `module-setup.json`: a module's table and power (web packages/modules
  setup.ts, desktop src/core/ModuleSetup.h): what a placed module's
  `table` and `power` read as, which setups the server takes
  (`PATCH /api/modules/:id`), and the layout summary a coordinator sees.
  `BLD_WRITE_CONTRACTS=1 pnpm --filter @cld/modules test` writes the
  answers.
