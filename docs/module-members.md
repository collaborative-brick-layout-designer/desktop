# A module's parts in the shared live layout (doc shape 3)

The same file is in both repos (`docs/module-members.md`). It describes only
the shared live document (the Yjs doc). Files on disk (`.bbm`,
`.bld-layout`, the sidecar) are not changed by any of this.

## The problem

A module is one value in the shared layout: `sidecarItems["m:<id>"]`, a plain
object with the module's `members` list in it. When two people change the
same module before either hears of the other, Yjs keeps one whole value and
drops the other, so one person's added or removed part is lost. The web put
its own lost change back (`moduleMergeGuard.ts`); the desktop could not.

## The shape

`sidecarItems` (the root map that already holds `m:<id>`, `l:<id>` and
`__blob`) gets one more kind of key, one per part added to or taken out of a
module:

    p:<n>:<moduleId>:<partId>     n = the module id's length in UTF-16 units

with a plain value:

- a number (finite, 0 or more): the part is in the module; the number is
  where it sorts among the parts added this way;
- `false`: the part was taken out of the module's starting list.

The module's value `m:<id>` keeps the `members` list it was made with (its
**starting list**). Nobody rewrites that list after the module exists. The
module's parts are then:

1. the starting list, in its order, without the ids whose key is `false`;
2. then every id whose key is a number and that isn't in the starting list,
   by number, then by id.

Writing a change to a module's parts writes only keys:

| change | write |
|---|---|
| add a part that isn't in the starting list | set its key to (highest number in the module + 1) |
| take out a part added by key | delete its key |
| take out a part of the starting list | set its key to `false` |
| put back a part of the starting list | delete its key |

Each key is its own Yjs value, so two people's adds and removes on one
module merge. A rename (which rewrites `m:<id>` with the same starting list)
no longer clashes with a part change. The same part added and removed at the
same instant ends up one way or the other, the same for everyone.

`meta.cache` stays what it is today: the whole sidecar, with every module's
full `members`, rewritten from the merged items when it lags
(`repairSidecarCache`). `__blob` still says which `meta.cache` the items go
with. A writer always writes `meta.cache` in the order the rules above give,
so the web and the desktop never rewrite it back and forth.

Deleting a module deletes its `m:` key and its `p:` keys. A `p:` key whose
module is gone is ignored. Parts that are no longer on the map are left out
when reading, as before.

## Old layouts: nothing to convert

An old layout is already a valid new one: a module with a `members` list
and no `p:` keys. So there is no conversion step to run on opening, and
nothing for two clients to race on: whichever client opens it first, the
layout reads the same, because no one writes anything.

(Turning each old list into keys was the first idea. It can lose a change:
two clients converting the same list at the same instant each write the
same keys, and a part one of them then takes out can come back through the
other's copy of the key. Keeping the list as the starting point has no such
window, and writes nothing for a module nobody changes.)

A layout moves to **doc shape 3** (`meta.schemaVersion` = 3) the first time
someone writes a `p:` key. That stamp is its own step, never undone, sent
with the change that needs it. Any app that finds `p:` keys in a doc stamped
lower (two stamps written at the same instant, one of them 2) stamps it 3
again. A layout whose modules nobody changes stays in the shape it has.

## Mixed versions

- **A newer app, an older layout (shape 1 or 2):** reads it as is, and
  keeps it that way until someone adds or removes a module's part.
- **An older app still connected when the layout moves to shape 3:** a
  change it made to the module's list at that instant is kept (its list
  becomes the starting list; the newer app's keys apply on top). It then
  sees the stamp and stops, as below.
- **An older app, a shape 3 layout:** it would read each module's starting
  list and miss the keys, so it must not open it. Both apps already refuse
  a doc stamped higher than they know (`canReadDoc`; `compat/compat.json`
  `doc.schemaVersion`): the web shows "this layout needs a newer version,
  reload", the desktop ends the live session and asks for an update. It
  never writes, so nothing is damaged. Edits it made offline stay in its
  own file.
- **A newer desktop, an older server:** the web app such a server serves
  would refuse a shape 3 layout, so the desktop doesn't make one there.
  A server that reads the keys lists the feature `moduleMemberKeys`
  (`/api/version`, `compat.json`); on a server without it the desktop
  writes a module's list whole, as before (and still reads and writes keys
  in a layout that already has them). So the desktop release can go out
  before the server is updated, and nobody is shut out.
- **Files:** a file saved or downloaded from a shape 3 layout is the same
  file as before (a full `members` list per module), so an older desktop
  still opens the file.

`compat.json`: `doc.schemaVersion` 3, with the reason under `doc.why`. The
desktop release that carries this is the first to read shape 3;
`desktop.recommended` should be raised to it when it is released.

## Undo

Undo and redo are Yjs's own: undoing an added part deletes its key, undoing
a part taken out deletes its `false`, and other people's keys are left
alone. The shape 3 stamp is never undone.

One thing had to be closed in both apps, and it is older than the keys.
When two people write the same sidecar value at the same instant (both
write `meta.cache` whenever they change a module or a label) and the one
whose value was kept then undoes, Yjs takes that value back and, with the
other person's value in between, puts nothing in its place: the key is
gone, and for `meta.cache` that is the whole sidecar. The web's repair
covered this except in the instant before it ran; the desktop had no
repair. Now both write such a value again as it was, after any undo or
redo that leaves it missing (web `keepSidecarThroughUndo`, desktop
`SyncDoc` `restoreSidecar`); `meta.cache` gets its modules and labels from
the items as they are now, so the undo still takes back what it should.

Known limit, desktop only: when two people make the very same change to
the very same part of one module at the same instant (both add it, or both
take it out) and the desktop then undoes its own, the part follows the
undo, though the other person made the same change. The web can tell the
two apart (it can ask Yjs what a value replaced) and keeps the other
person's change.

## Offline on the desktop

The desktop keeps its offline edits apart and merges them on reconnecting
(`LayoutMerge`). A module both sides changed used to be a clash to ask
about. Now the parts I added or took out go on top of the server's list,
and so does a field only one side changed; it is asked about only when
both changed the same field (its name, say) to different values. The web
has no such step: its offline edits are in the Yjs doc and merge by the
keys.

## The server

Stores the doc as it does now: no database change. The live checks accept
the new keys and refuse a malformed one: a `p:` key that doesn't parse, or
whose value is anything but `false` or a finite number from 0 up
(`ws/memberGuard.ts`). Readers skip such a value too. On opening a room the
server stamps shape 3 when it finds `p:` keys under a lower stamp.

## Shared test vectors

`fixtures/contracts/module-members.json`, the same in both repos: starting
lists and keys with the parts they read as, and wanted lists with the keys
a writer must write.
