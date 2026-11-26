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
  design 4), and on loops (an oval, a figure eight, a dog bone, hairline
  gaps, part of the Fordyce 2026 layout): the way found for a train leaving
  the start one way or either way, its length, the switches it needs (and
  any it needs both ways) and where it turns the train round, or why there
  is none; the loose ends that nearly meet (joined for trains); and each
  route checked again with a part taken out (web packages/parts-catalog/src/track/routes.ts,
  desktop src/track/Routes.cpp). The web's
  `BLD_WRITE_CONTRACTS=1 pnpm --filter @cld/parts-catalog test` writes the
  `expect` and `status` values.
- `cuts.json`: cars on the track (Live Ops design 2.3; web
  packages/parts-catalog/src/track/cuts.ts, desktop src/track/Cuts.cpp):
  what trains and cars people changed at once read as, what dragging a car
  does (which cars come, what they couple to), taking one off the track,
  and the limits. The web's
  `BLD_WRITE_CONTRACTS=1 vitest run src/track/cuts.test.ts` writes the
  `expect` values.
- `ops-routing.json`: waybills, jobs and the routing of cars (Live Ops
  design 2.4, 3; web packages/parts-catalog/src/track/ops.ts, desktop
  src/track/Ops.cpp): records as read, a waybill's turning, Make waybills
  from a seed, where a car stands, and for places, cars and jobs, every
  car's next move, the switch lists and what can't be routed. The web's
  `BLD_WRITE_CONTRACTS=1 vitest run src/track/ops.test.ts` writes the
  `expect` values.
- `ops-session.json`: operating sessions (Live Ops design 2.5, 4; web
  packages/parts-catalog/src/track/opsSession.ts, desktop
  src/track/OpsSession.cpp): for each role in a session, what it may move
  and flip (the server refuses the same, apps/server/src/ws/opsGuard.ts),
  with no session, a paused one, two running, and one copied into another
  layout; and the start of a session, putting every car back, the log
  played again and a session's summary. The web's
  `BLD_WRITE_CONTRACTS=1 vitest run src/track/opsSession.test.ts` writes
  the `expect` values.
- `train-geometry.json`: trains on the route-search network (trains design
  3.3): rolling stock records, and where each car's trucks, middle and
  couplers sit for a train's head on the track, through switches as set,
  and when it runs off the end (web packages/parts-catalog/src/track/trains.ts,
  desktop src/track/Trains.cpp). The same command writes the `expect` values.
  Its `overhang` (a car's swing on a curve), `runs` (the track between
  switches and ends, and the length a train can stand in) and `checks`
  (the fit checks for trains and routes: kind, severity, where, numbers)
  are the fit checks (trains design 3.5; web fitChecks.ts, desktop
  src/track/FitChecks.cpp).
- `truck-guess.json`: where an imported model's trucks and couplers seem
  to be (trains design 3.3, Phase 6; web
  packages/parts-catalog/src/track/stockGuess.ts, desktop
  src/track/StockGuess.cpp): the part table both keep word for word
  (`parts`), and vectors of a model's parts from above with its footprint
  and picture's middle, and the guess (`expect`). Three vectors read
  `truck-guess-*.mpd` with each app's own LDraw reader: LDraw Official
  Model Repository train models by TotalyWicked (10020-1 Santa Fe Super
  Chief, 10014-1 Caboose, 10016-1 Tanker), redistributable under CCAL 2.0
  (their `!LICENSE` lines); kept byte for byte (`-text` in
  .gitattributes). The expect values were worked out by hand; change the
  table or the rules in both apps and both tests at once.
- `train-animation.json`: running trains on the route-search.json network
  (trains design 5): a scale speed as studs a second and real metres a
  second; trains run frame by frame, on a route or following the
  switches, with what happens on which frame (a route's switch set, a
  trailing point pushed through, a route's end, the end of the track, a
  collision: both stop), the switches the run flipped (never the
  layout's), and where each head is after; and whether two trains standing
  on the track touch (web packages/parts-catalog/src/track/animation.ts,
  desktop src/track/Animation.cpp). The same command writes the `expect`
  values.

To change them: edit (or, for the imported part, run the desktop's
`bld_sync_tests --gtest_filter='Contracts.*'` with `BLD_WRITE_CONTRACTS=1`),
then `sha256sum cuts.json doc-shape.json imported-part.json imported-part.xml module-members.json module-setup.json ops-routing.json ops-session.json part-naming.json places.json presence-colors.json route-search.json shared-module-keep.json shared-module-placement.json shared-module-settle.json sidecar-hash.json track-kinds.json track-network.json track-paths.json train-animation.json train-geometry.json truck-guess.json truck-guess-10014-1.mpd truck-guess-10016-1.mpd truck-guess-10020-1.mpd update-guard.json > MANIFEST.sha256`
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
- `places.json`: places (Live Ops; web packages/bbm/src/places.ts, desktop
  src/core/Places.cpp): the limits, what a stored place reads as and its
  size, which places the server takes (ws/placesGuard.ts), and what
  happens to places when a module is copied, updated from the Module
  library, saved to it, made of parts or taken apart; the counts and the
  cars a spot holds. `onTrack` is where a spot is on the track of
  route-search.json and how much a train can stand in (web
  packages/parts-catalog/src/track/places.ts, desktop
  src/track/PlacesOnTrack.cpp). `BLD_WRITE_CONTRACTS=1 pnpm --filter
  @cld/bbm test`, then the same for `@cld/parts-catalog`, writes the
  answers.
