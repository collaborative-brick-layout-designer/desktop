# Shared modules: read-only sharing with updates (design)

Status: **design, for a decision.** No code has been written for it. The
same file is in both repos (`docs/shared-modules.md`).

Aaron, 2026-11-14: "make some architecture plan for sharing modules between
users or a club for like a specific layout and would get updates when the
user updates their modules", then: "it would be like read only sharing for
another user or club".

**In one line:** a module's owner shares it, read-only, with a person or a
club. They can place it in their layouts (a club's show layout, say) and
move, turn and put it on a sheet, but not change its parts. When the owner
saves a new version, every placed copy can follow it.

Contents:

1. [What already exists](#1-what-already-exists)
2. [The model: a reference, or a copy?](#2-the-model-a-reference-or-a-copy)
3. [Updates](#3-updates)
4. [Collisions after an update](#4-collisions-after-an-update)
5. [Permissions](#5-permissions)
6. [Keeping it read-only](#6-keeping-it-read-only)
7. [The UI](#7-the-ui)
8. [Data: migrations, routes, the doc, compat.json](#8-data)
9. [Phases](#9-phases)
10. [Decisions for Aaron](#10-decisions-for-aaron)

---

## 1. What already exists

Most of the pieces are there. What's missing is the lock, sharing with a
club, and updates that reach every layout.

| Piece | Where | What it gives us |
|---|---|---|
| Module library | `modules`, `module_versions` (20 kept), `PUT /api/modules/:id/snapshot` with `If-Match` | Each save is a numbered version with a "What changed?" note and a picture. |
| Sharing with a person | `module_collaborators` (`viewer`/`editor`), `module_invites` (`/api/module-invites/:token`) | A person can already be given a module to view, by email link. |
| A placed module's link | sidecar `m:<id>` → `libraryModuleId`, `libraryVersion` (web `editor/moduleLibrary.ts`, desktop `edit/ModuleLibraryLink.cpp`) | A placed module already knows which library module and version it came from. "Update from Module library" puts a newer version in, where the module sits now, asking first when it was changed in the layout. |
| Finding the place again | `alignToPlaced`, `matchesVersion` (both apps) | Works out how a version sits in the layout from the parts the two have in common; tells whether the placed copy was changed. |
| "Update available" model | sets: sidecar `sets`, `findSetUpdates`, `SetUpdatesBar` (both apps); catalog copies: `GET /api/catalog/copies` | Notices with Show / Update / Not now. Nothing updates by itself. |
| Module parts in the live doc | doc shape 3, `p:<n>:<moduleId>:<partId>` keys (`docs/module-members.md`) | Two people changing one module's parts at once both keep their change. |
| Pinned modules | sidecar `pinned` | A module that can't be moved as a whole. The nearest thing to a lock today. |
| Live checks | `ws/updateGuard.ts`, `memberGuard.ts`, `mergeGuard.ts`, `liveAccess.ts` | The server checks every incoming change before it is applied, and re-checks access at once when a role changes. A refused change closes that connection, so refusals are for broken or outdated apps, not for normal use. |
| Live hints | `/api/events` (`events/hub.ts`), desktop `sync/EventStream` | "Module X changed, now version v" already reaches its owner and collaborators, in both apps. |
| Custom parts that come along | `custom_part_set_shares` (0038) | Sharing a set brings the sharer's own parts it is made of, as a viewer, and takes them back with it. |
| Clubs | club roles and permissions (`view_items`, `edit_layouts`, `edit_modules`, `invite`, …), club collections | Per-club permissions; club-owned modules. |
| Recently deleted | `trash_items` | A deleted module waits 30 days and can come back. |

Two gaps in today's code that this design has to close:

- **Club co-editors can't see the library module.** `libraryState` looks the
  module up in *your own* module list. If Bob places Alice's module (shared
  with Bob) in the club layout, Carol, who edits the same layout, sees "its
  Module library copy is gone".
- **Two people pressing Update at once doubles the parts.** `replaceModuleParts`
  deletes the old parts and inserts new ones; two clients doing that in the
  same instant each insert a full set.

## 2. The model: a reference, or a copy?

### Option A: a reference

The layout holds only "module M, version 7, here, turned 90°, on sheet
Track". The parts are drawn from the owner's module.

- Live rooms: small doc. But every renderer (web canvas, desktop scene,
  export, print, parts list, budget, snapping, venue checks, LDraw/Studio
  export) has to learn to draw parts that aren't in the doc.
- Files: a `.bbm` and a `.bld-layout` must hold real parts, so each export
  has to fetch every shared module and expand it. With no server (desktop
  offline, a file emailed to someone), the module can't be drawn at all.
- BlueBrick: sees nothing unless we expand on export. A `.bbm` opened and
  saved back loses the reference.
- Desktop offline: needs its own cache of every module version used.
- Server: stores each module once. Cheapest.

### Option B: a read-only copy, linked (recommended)

The placed module's parts are in the layout's doc, as they are today, and
the module is marked **shared** (read-only) with its link and its exact
placement. An update replaces those parts with the new version's, at the
same placement.

- Live rooms: nothing new to draw. Every tool, export and check keeps
  working, because the parts are real parts.
- Files: `.bbm` and `.bld-layout` hold the parts with no extra step.
- BlueBrick: sees ordinary parts. The lock and link live in the sidecar,
  which BlueBrick never sees (as `libraryModuleId` does today).
- Desktop offline: has everything. Only updates wait for the network.
- Server: a copy per placing layout (a module is typically tens to a few
  hundred parts, so kilobytes) and some Yjs history per update. See
  "history growth" in section 9, phase 3.

### Option C: a copy plus a reference

Option B, and the doc also keeps a hash of the version so a client can
redraw from the owner's module. That adds A's costs without removing
any of B's.

**Recommendation: B.** It is the model we already have (a linked placed
module), so this feature is "lock it, share it wider, and send the updates
everywhere", not a new way of drawing layouts. A is the better fit only
if layouts never left the server, and they do (files, BlueBrick, offline
desktop).

### What a shared placed module holds

The sidecar module value `m:<id>` gets one more field, `shared`, next to
the link fields it already has:

```jsonc
{
  "id": "…", "name": "Harbor crane", "members": ["…"],
  "libraryModuleId": "5f0c…",       // as today
  "libraryVersion": 7,              // as today
  "shared": {
    "by": "Alice",                  // owner's public name when placed, for files with no server
    "server": "https://example.org",// where the module lives (several servers, desktop)
    "placement": { "degrees": 90, "x": 120.5, "y": -48, "sheets": { "Track": "<layer id>", "Scenery": "<layer id>" } }
  }
}
```

`placement` is how the version's own coordinates map onto the layout: turn
by `degrees` about the version's origin, then move to (`x`, `y`); each of
the version's sheets goes to a layout sheet. Both apps update it whenever
the module is moved, turned or a sheet is changed. With it an update lands
exactly, with no guessing. (The existing `transform` field is always the
identity in both apps and isn't touched.) If `placement` is missing or
doesn't match the parts (an older app moved the module), the apps fall back
to `alignToPlaced`, as "Update from Module library" does now.

## 3. Updates

### When an update goes in

| Option | What happens | For | Against |
|---|---|---|---|
| **U1. Always live** | The owner saves; every placing layout changes at once. | What "get updates" sounds like. | A show layout changes under the club the night before the show; a new footprint can overlap the neighbors with nobody watching. |
| **U2. Update available** | A notice in the layout; someone who can edit the layout presses Update. | Nothing moves without a person looking. Same as sets and catalog copies today. | A busy club misses updates. |
| **U3. Each layout chooses** (recommended) | Layout setting **Updates from shared modules: Ask first (default) / Automatically**. Ask first is U2. Automatically puts each new version in as soon as it is saved, then tells the layout's editors what changed and where it now overlaps. | The club decides; a show layout can be set to Ask first a week before the show. | One more setting (in Layout settings, with ⓘ help, per "settings in the UI"). |

**Recommendation: U3**, with U2 (Ask first) shipped first and Automatically
in phase 2. Either way, an update **never blocks** on collisions (section 4).

### Who applies an update: the server

The server applies every update, for live rooms and closed layouts alike:

- **One writer.** Two people pressing Update at once can't double the parts:
  the server applies version 8 once, and the second press finds it already
  at 8. The same closes the gap in today's "Update from Module library".
- **Closed layouts and Automatically.** With nobody in the room, the server
  opens the stored doc, applies, saves (as it already does for its own
  repairs, such as the shape-3 stamp on opening a room).
- **Undo.** A server change is in nobody's undo stack. The notice offers
  **Go back to version 7** instead, which is the same server step with the
  older version (20 versions are kept). The person's own moves stay
  undoable as now.

The placement math (turn, move, sheet mapping, part pivots) moves to a
shared package (`packages/modules`) that the server and the web use, with
shared test vectors (`fixtures/contracts/shared-module-placement.json`)
that the desktop's C++ copy also passes, like the module-members vectors.

**The desktop applies its own updates only when the layout isn't live**:
a layout opened from a file, or a live layout while offline. Then the
desktop fetches the version, applies it locally with the same math, and the
change is the person's own undo step. When an offline desktop reconnects,
`LayoutMerge` treats a shared module the server updated meanwhile as the
server's: its parts are the server's version, and the desktop's offline
move of it is put back on top through `placement`.

### How an update reaches people

| Where | How |
|---|---|
| Open editor (web or desktop, live) | The module hint (`kind: 'module'`, `v`) also goes to everyone who can edit a layout that places it (via the uses table, section 8). The editor refetches the layout's shared-module list and shows the notice, or, with Automatically, the server has already written the change into the room and the editor shows "Updated to version 8". |
| Layout lists (web Layouts page, desktop Open from server) | A small "2 updates" badge on the layout, from the uses table. |
| Closed layouts with Automatically | Already updated; the layout's page and the next open show "Updated while you were away" with the list. |
| Desktop file / offline | On open (and on reconnect), the desktop asks the module's server for the latest versions of the shared modules in the file; when it can't reach it, the Modules panel says "Can't check for updates (offline)". |

## 4. Collisions after an update

A new version can be bigger, a different shape, or have its track ends
somewhere else.

| Option | What happens |
|---|---|
| C1. Block | The update waits until it fits. Leaves layouts stuck on old versions over a 1-stud overlap. |
| C2. Warn before | Ask first shows what would overlap before Update. Doesn't work for Automatically. |
| **C3. Never block, mark after** (recommended) | The update goes in. The module then carries a mark: **overlaps 2 parts**, **2 track ends no longer meet**. The notice lists them with **Show**. The mark goes when it's fixed or dismissed. Ask first also shows a preview outline of the new footprint before Update. |

The checks are the ones the apps already have: footprint overlap (the same
test the venue checks use) and connection points that met before and don't
now (the snap code knows both). Non-blocking, like the venue checks.

**Recommendation: C3.** A layout editor can then move the module (allowed),
move the neighbors, or Go back to the older version.

## 5. Permissions

### Who can share, with whom

| Share | Who can do it | Recommended |
|---|---|---|
| Person → person | The module's owner: its owner, or for a club module whoever has owner rights on it (club Admins and Managers). | Yes. Reuses `module_collaborators` with role `viewer` and the existing email invites. |
| Person → club | The module's owner, if they are a member of that club. | Yes. New `module_club_shares`. A module of yours shared with your club is still yours: the club can't change or delete it. |
| Club → another club | Club module owners. | Later, if asked. |
| Re-sharing | Nobody but the owner. Viewers and club members can't pass it on. | Yes. |
| Club module, inside the club | Already visible to members by club permissions; this design adds the lock when it's placed (decision D5). | |

### Where it can be placed

- **Shared with you:** in layouts you can edit whose owner the module is
  also shared with: your own layouts, or a club's layout if it is shared
  with that club too. If Bob tries to put Alice's module (shared with Bob
  only) into the club's layout, he gets: "Alice shared this module with
  you, not with Riverside LUG. Ask Alice to share it with the club." (with
  a button that copies a short message). This keeps the owner deciding
  where it goes, and everyone who edits that layout can then see its
  updates. (Decision D3.)
- **Shared with a club:** in that club's layouts, by members who may edit
  them (`edit_layouts`). Not in their personal layouts, unless the owner
  also shares it with them. (Decision D4.)

### What the receiver sees

The module's name, picture, parts list, version numbers and "What changed?"
notes, and the owner's public name (as `publicName.ts` gives it). Not the
owner's other modules or layouts. The owner's custom parts that the module
uses come along read-only for the same people, the way a set's parts do
(0038), so nothing draws as a placeholder.

The owner sees, on the module's Sharing tab, who it is shared with and
**Used in 4 layouts** with the club names. Not the titles of other
people's private layouts. (Decision D7.)

### When something changes

| Event | Placed copies | Notes |
|---|---|---|
| Owner stops sharing | Stay, read-only, at the version they have. Marked "No longer shared: no more updates". A layout editor can **Remove** it, or **Make it mine** (D6). | The parts must stay: layouts and files are made of real parts, and a club's show plan can't lose a module silently. The stop-sharing dialog says so plainly. |
| Owner deletes the module | Same as stop sharing, marked "Deleted by its owner". Restoring it from Recently deleted brings the updates back. | |
| Owner's account is erased | Same, and the lock comes off: they become ordinary modules ("Builder #…" credit, as now). Nobody is left whose design the lock protects. | |
| A member leaves the club | Nothing changes in the club's layouts. They lose the club's shares, like any club thing. | |
| A role changes (loses `edit_layouts`) | They can't place, update or detach anymore. Open editors see it at once (`notifyAccessChanged`). | |
| The module changes owner (transfer) | Shares and placements carry over; the new owner's saves are the updates. | |
| The layout changes owner | Placements whose module isn't shared with the new owner become "No longer shared". The move dialog lists them first. | |

## 6. Keeping it read-only

Three layers; each catches what the one above misses.

**1. The editing tools (both apps).** A shared module is picked as a whole,
always (as modules are now). Allowed: move, turn, change its sheet, pin it,
its outline/name colors and "show name" (those are the placement's, not the
module's), delete the whole module, duplicate it (the copy is another
shared placement), copy and paste it into another layout (also shared, if
that layout may have it). Not allowed, each with a plain reason instead of
a silent no:

- Edit module → "Harbor crane is shared by Alice, so its parts can't be
  changed here. ⓘ"
- deleting, recoloring or replacing one of its parts, Edit part, Find and
  replace over its parts (skipped, and the result says how many were
  skipped);
- renaming: it is the owner's name. (A layout label can be added on top.)

**Undo** stays each person's own. It can only reverse what they did, which
was allowed. Undoing "Make it mine" puts the lock back. Since updates keep
the unchanged parts (phase 3, doc shape 5), an earlier undo step can point
at some of a module's parts and not the new ones. So both apps finish
every undo and redo for the whole module in the same change
(`packages/modules` settle.ts; shared vectors
`fixtures/contracts/shared-module-settle.json`):
- some parts moved back: the rest move the same way;
- some parts went (undoing placing it): the rest go, with the module;
- the module's record went: its parts go too.

**2. The server guard** (`ws/sharedGuard.ts`, run with the other checks).
For parts that belong to a shared module, catch an update that changes a
part's number, color or other fields, deletes one without deleting the
whole module (a sheet change deletes and re-adds the same id, allowed),
writes `p:` keys for the module, or rewrites its `members`. Only the
server's own update step and Make it mine change those. It also checks
that a move is rigid (phase 3): every part turned by the same amount, and
the distances between the parts' centers kept, within 0.05 studs for float
rounding. A center is the part's pivot, from the parts' geometry the
server reads ahead per layout (`ws/sharedGeometry.ts`). Until that has
been read, or for a part no catalog knows, the box center is used, and a
turned module lets such a part off by its size. Two people moving the
module at the same moment are never measured against each other: the
layout then holds some of each person's move.

Normal apps never send such a change (the tools don't allow it), so this
is for an outdated or broken app. Such an update is **taken, then put
back**: the server restores the modules it broke, in its own step that
everyone gets (the sender too), and the connection stays open. Closing
the connection, as the other guards do, would leave the change in the
app's copy. A desktop saves that copy and sends it again each time the
layout is opened, so it would be refused again every time. Putting the
module back works the same for every app, old ones included. It is logged
and audited (`layout_update_refused`, kind `locked`, `putBack`).

**3. Drift check (files and BlueBrick).** A file can be changed anywhere:
in BlueBrick, by an older desktop, by hand. On opening, both apps compare
the shared module's parts with its version (`matchesVersion`). If they
differ, the module is marked "Changed outside the app" with **Put back
Alice's version** (the server step, or a local step for files) and **Make
it mine**. The lock isn't secret-keeping: anyone who can see the parts can
rebuild them, as viewers can already copy a module today. It keeps
layouts in step with the owner and stops accidental changes.

### "Make it mine" (detach)

The placed copy becomes an ordinary module of this layout: unlocked, no
more updates, credited "based on Harbor crane by Alice" as copies are now.

| Option | |
|---|---|
| Anyone who can edit the layout | Simple; quickest way to fix a local problem. |
| **The layout's owner (or club Admins/Managers for a club layout)** (recommended) | A plain member can't quietly fork a shared module in the club's layout. |
| Only if the owner allows it (a share setting) | Most control; one more setting, and "Copy to my modules" already lets viewers copy. |

## 7. The UI

Plain words, ⓘ help on every new idea, no dead ends, and everything
reactive: a share, a stop-sharing or a new version shows up everywhere
without a refresh (live hints). The same in both apps.

### Sharing a module (Module library → module → Share)

```
┌ Share "Harbor crane" ────────────────────────────────── ✕ ┐
│                                                            │
│  People and clubs you share with can place this module in  │
│  their layouts and get your updates. They can't change     │
│  its parts.  ⓘ                                             │
│                                                            │
│  Add  [ email or club ▾                       ] [ Share ]  │
│                                                            │
│  Riverside LUG (club)          can place it    [ Stop ]    │
│  bob@example.org               can place it    [ Stop ]    │
│  carol@example.org             can change it   [ ▾ ]       │
│                                                            │
│  Used in 4 layouts (Riverside LUG: 3, Bob: 1)              │
│                                                            │
│  Clubs you're in: Riverside LUG, Train Club North          │
└────────────────────────────────────────────────────────────┘
```

"Can place it" is the read-only share (viewer). "Can change it" is today's
editor share, unchanged. Stopping a share asks:

```
┌ Stop sharing with Riverside LUG? ─────────────────────────┐
│  Its 3 layouts keep Harbor crane as it is now (version 7). │
│  They won't get your updates anymore.                      │
│                                      [ Cancel ] [ Stop ]   │
└────────────────────────────────────────────────────────────┘
```

### Shared with me (Module library panel and page)

A new filter next to Mine / Clubs: **Shared with me**, each module with
its owner, and "via Riverside LUG" for club shares. Places only where it
may go (section 5); dragging it elsewhere explains why and how to fix it.

```
Module library   [ Mine | Clubs | Shared with me ]   🔍
┌──────────┐ Harbor crane            v7 · Alice
│ picture  │ via Riverside LUG       [ Place ]  ⓘ
└──────────┘
┌──────────┐ Mountain tunnel         v3 · Dan
│ picture  │ shared with you         [ Place ]
└──────────┘
```

### Placing

Same as placing any module now (drag, or Place then click). The first time,
a one-line tip: "Shared modules follow their owner's updates. You can move
and turn them; their parts stay as the owner made them."

### On the map

A shared module gets a small link badge at its name and a dashed outline in
its outline color (so it reads as "not yours to change" at a glance, also in
the dark theme and for color-blind users, since the badge carries it, not the
color):

```
   ┌ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┐
     🔗 Harbor crane · Alice
   │   [parts as usual]   │
     ● update available
   └ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┘
```

The Modules panel shows the same, with the state in words: "Shared by
Alice · v7", "v8 is ready", "No longer shared", "Changed outside the app",
"Overlaps 2 parts".

### The update notice (Ask first)

Over the bottom of the map, like the set updates bar:

```
┌──────────────────────────────────────────────────────────────────┐
│ Harbor crane has a new version from Alice (v8): "longer pier".   │
│ It's 6 studs wider and would overlap 2 parts.                    │
│                       [ Show ]  [ Update ]  [ Not now ]   ⓘ      │
└──────────────────────────────────────────────────────────────────┘
```

Show zooms to it and draws the new footprint as an outline. Several at
once: "3 shared modules have new versions" with **Update all** and a list.

After an update (or after an automatic one):

```
┌──────────────────────────────────────────────────────────────────┐
│ Harbor crane is now version 8. It overlaps 2 parts.              │
│              [ Show ]  [ Go back to version 7 ]  [ OK ]          │
└──────────────────────────────────────────────────────────────────┘
```

### Module context menu (shared)

```
Harbor crane · shared by Alice
  What changed (versions)…
  Update to version 8
  Go back to an older version…
  ─────────────
  Pin in place
  Outline and name color…
  Duplicate
  Remove from this layout
  ─────────────
  Make it mine…            (layout owner only; others see why it's greyed)
```

### Layout settings

```
Updates from shared modules   (•) Ask first   ( ) Automatically   ⓘ
  ⓘ Automatically: a new version goes in as soon as its owner saves it.
    You'll see what changed and anything it now overlaps.
```

### Guided tour

One new stop in the module tour: the Shared with me filter, the badge, the
update notice.

## 8. Data

### Migrations (additive only)

`0050_shared_modules.sql`:

```sql
-- A module shared, read-only, with a club.
CREATE TABLE module_club_shares (
  module_id  TEXT NOT NULL REFERENCES modules(id) ON DELETE CASCADE,
  org_id     TEXT NOT NULL REFERENCES orgs(id)    ON DELETE CASCADE,
  shared_by  TEXT REFERENCES users(id) ON DELETE SET NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (module_id, org_id)
);
CREATE INDEX module_club_shares_org_idx ON module_club_shares(org_id);

-- Where each shared module is placed (kept by the server from the docs it
-- stores; can be rebuilt from them). For update badges, the owner's
-- "Used in", Automatically, and access through a layout.
CREATE TABLE layout_shared_modules (
  layout_id   TEXT NOT NULL REFERENCES layouts(id) ON DELETE CASCADE,
  placed_id   TEXT NOT NULL,            -- the module's id in the layout
  module_id   TEXT NOT NULL,            -- no FK: placements outlive the module
  version     INTEGER NOT NULL,
  state       TEXT NOT NULL,            -- 'shared' | 'unshared' | 'gone'
  updated_at  INTEGER NOT NULL,
  PRIMARY KEY (layout_id, placed_id)
);
CREATE INDEX layout_shared_modules_module_idx ON layout_shared_modules(module_id);

-- The owner's custom parts a shared module uses, shared along with it
-- (as custom_part_set_shares does for sets).
CREATE TABLE module_share_parts (
  module_id  TEXT NOT NULL REFERENCES modules(id)      ON DELETE CASCADE,
  part_id    TEXT NOT NULL REFERENCES custom_parts(id) ON DELETE CASCADE,
  user_id    TEXT REFERENCES users(id) ON DELETE CASCADE,
  org_id     TEXT REFERENCES orgs(id)  ON DELETE CASCADE,
  added_at   INTEGER NOT NULL
);

ALTER TABLE layouts ADD COLUMN shared_module_updates TEXT NOT NULL DEFAULT 'ask'; -- 'ask' | 'auto'
```

Person shares reuse `module_collaborators` (`viewer`) and `module_invites`;
no change there. Privacy export and account erasure gain the new tables
(`privacy/collect.ts`, `accountDeletion.ts`, `clubDeletion.ts`).

### Routes

| Route | What |
|---|---|
| `GET /api/modules?owner=shared` | Shared with me, including club shares (`sharedVia`). |
| `GET /api/modules/:id/shares` | People, clubs, and uses count (owner only). |
| `POST /api/modules/:id/club-shares` `{orgSlug}` / `DELETE …/:orgId` | Share with a club / stop. |
| `GET /api/layouts/:id/shared-modules` | The layout's shared modules: placed id, module, owner, version, latest, latest note, state, overlaps. Any layout viewer. |
| `GET /api/layouts/:id/shared-modules/:placedId/versions/:n/snapshot` | A version, seen through the layout (so club co-editors can preview it without their own share). |
| `POST /api/layouts/:id/shared-modules/:placedId/apply` `{version}` | Server applies that version (newer: Update, older: Go back). Layout editors. Idempotent. |
| `POST /api/layouts/:id/shared-modules/apply-all` | Update all. |
| `POST /api/layouts/:id/shared-modules/:placedId/detach` | Make it mine (D6). |
| `PATCH /api/layouts/:id` `{sharedModuleUpdates}` | The layout setting. |

Live hints: a module save also goes to editors of layouts in
`layout_shared_modules`; a server-applied update is an ordinary room change
plus a `layout` hint with action `shared-module-updated`.

### The live doc: shape 5 (phase 3)

An update that keeps the unchanged parts (`packages/modules` keepIds.ts,
`updateModuleParts`) stamps the layout **shape 5**, in its own step before
the update, with a mark (`meta.keptIds`) that raises a lower stamp again on
opening. Builds that read only 4 can't finish an undo across such an update
(see "Undo" above), so they refuse the layout. The server lists the feature
`sharedKeepIds`. A desktop keeps the ids in its own update of a live
layout made offline only on a server that lists it; in a file it always
does.

The matching rule:
- each part of the new version, in its order, takes the first of the
  module's parts not yet taken that is on the same sheet, with the same
  part number and height, within 0.1 studs and 0.5°;
- on each sheet the kept parts must already be in the version's order (the
  longest run in order stays);
- their sets must match one for one, or nothing is kept;
- the other parts go, and the new ones go in each next to its neighbor in
  the version.

Shared vectors: `fixtures/contracts/shared-module-keep.json`. A layout's
history grows by what changed: for a 200-part module with 3 parts changed,
about 390 bytes instead of about 20,300.

### The live doc: shape 4

Yes, it needs a **doc shape 4**. An app that doesn't know `shared` would
let people change the parts, and the server would then refuse its changes
and drop its connection, again and again. With a stamp it is told plainly
instead ("This layout needs a newer version"), as with shapes 2 and 3.

- A layout moves to shape 4 when the first shared module is placed in it
  (the stamp is its own step, never undone, as in shape 3). Layouts without
  shared modules stay as they are.
- **Mixed versions:** an older web tab or desktop in the room sees the
  stamp and stops writing; a newer desktop on an older server doesn't offer
  Shared with me (feature `sharedModules` not listed), so it never makes a
  shape-4 layout there.
- **Files:** sidecar `schemaVersion` stays 1 and `layoutFile.version` stays
  1. The `shared` field is extra, and older apps keep unknown fields. An
  older desktop opening such a file sees an ordinary module it can change;
  the drift check catches it when a newer app opens the file again.

`compat/compat.json` (both copies, one change):

- `doc.schemaVersion: 4`, with `doc.why["4"]`: "A module shared read-only by
  its owner (sidecarItems m:<id> `shared`) is locked; builds that read only
  3 would let its parts be changed, and refuse it."
- `features` + `serverFeatures`: `{ "id": "sharedModules", "label": "Modules shared with you and your clubs, with their owner's updates" }`;
  `desktopUses` gains it.
- `desktop.why` gains the first desktop release that reads shape 4, and
  `desktop.recommended` moves to it on release.

## 9. Phases

Sizes: S ≈ a day, M ≈ 2–4 days, L ≈ a week, for one agent, tests included
(journey/UI tests in the same PR, per our rule).

**Phase 1: share, place, lock, update by hand (the minimum that's useful).**

| Part | Size |
|---|---|
| Server: club shares, Shared with me, custom parts along, uses table kept from docs, `apply` (server-applied, idempotent) and Go back, shape-4 stamp, structural guard, compat.json | L |
| `packages/modules`: placement math moved out of the web editor, shared vectors | M |
| Web: Share dialog with clubs, Shared with me, placing rules with plain refusals, lock in all tools, badge and outline, update notice (Ask first), Modules panel states, stop-sharing and deleted states, tour stop | L |
| Desktop: the same, plus local apply for files and offline, `LayoutMerge` rule, update check on open | L |
| Collision marks after an update (overlap and broken track ends) | M |

**Phase 2: Automatically, and updates seen from outside the layout.**
Layout setting, server applying on save for `auto` layouts (rooms and
closed docs), badges in layout lists, "Updated while you were away",
owner's "Used in" view, drift check with Put back. **M–L.**

As built (2026-11-17):

- The setting is the `layouts.shared_module_updates` column from 0050,
  set with `PUT /api/layouts/:id/shared-modules/updates` `{updates: 'ask'
  | 'auto'}` (layout editors; the desktop with a `layouts:write` token),
  and read from `GET /api/layouts/:id` (`sharedModuleUpdates`) and
  `GET /api/layouts/:id/shared-modules` (`updates`). Nothing new goes in
  the doc, so no new doc shape. Switching to Automatically puts in what
  the layout was waiting on.
- A save (or a restored version) of a module puts the new version in
  every `auto` layout that places it and still follows it
  (`sharedModules/auto.ts`), with the same `apply` step as Update, in
  open rooms and stored docs. Each one leaves an audit event
  `shared_module_auto_update` and a `shared-module-updated` layout hint.
- `GET /api/layouts` gives each layout `sharedUpdates`: how many of its
  shared modules have a newer version waiting. The lists show it as a
  badge.
- "Updated while you were away": `layout_visits` (0051) keeps when each
  person last had a layout open (stamped when they leave its room, and
  by `POST /api/layouts/:id/shared-modules/away`, which the editor calls
  once on opening and which lists the automatic updates since).
- "Used in" was already in phase 1's Share dialog (both apps).

**Phase 3: hardening and polish.** Rigid-move check in the guard (S–M).
Updates that keep the ids of unchanged parts, so an update only touches
what changed and the doc's history grows less (M). Make it mine as a
request to the owner if D6 goes that way (S). Club → club shares if asked
(S). The same on the native mobile apps when they come.

## 10. Decisions for Aaron

Each with the recommended answer.

- **D1. Copy or reference?** Recommended: **a read-only copy in the
  layout, linked to the owner's module** (section 2, option B), so files,
  BlueBrick and offline keep working.
- **D2. When do updates go in?** Recommended: **each layout chooses: Ask
  first (default) or Automatically**; ship Ask first in phase 1. Updates
  never block on overlaps; they mark them.
- **D3. Can someone place a module shared with them personally into a
  layout someone else owns (a club's)?** Recommended: **no; it has to be
  shared with that layout's owner too**, so the module's owner decides
  where it goes and every co-editor gets its updates.
- **D4. Can club members use a club-shared module in their own personal
  layouts?** Recommended: **no, club layouts only**; the owner can share
  with them personally as well.
- **D5. A club's own module placed in a club layout: locked too?**
  Recommended: **no by default** (today's linked, changeable copy), with a
  per-placement choice "Keep it as the club's version (read-only, follows
  updates)". Club members who can edit club modules can change it anyway.
- **D6. Who may "Make it mine" (unlock a placed copy)?** Recommended: **the
  layout's owner, or club Admins/Managers on a club layout**.
- **D7. What does the module's owner see of where it's used?**
  Recommended: **a count and the club names**, not the titles of other
  people's private layouts.
- **D8. When the owner stops sharing or deletes the module, what happens to
  placed copies?** Recommended: **they stay, read-only, at their version,
  marked "No longer shared"**; editors can remove them or make them theirs.
  On account erasure the lock comes off.
- **D9. Doc shape 4?** Recommended: **yes**, stamped only on layouts with a
  shared module, so older apps are told to update instead of being cut off
  mid-edit.
- **D10. Can someone a module is shared with still "Copy to my modules"?**
  Today any viewer can (`POST /api/modules/:id/copy` only asks that you can
  see it). Recommended: **yes, keep it**: read-only means "placed copies
  follow the owner and can't be changed by accident", not "can't be
  copied", since the parts are in every file anyway. The other choice is a
  per-share "Allow copies" switch, off by default.
