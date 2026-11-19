# bld-import-server: the website's model import

The website imports LDraw (`.ldr`, `.mpd`, `.dat`), BrickLink Studio (`.io`)
and LEGO Digital Designer (`.lxf`, `.lxfml`) files by running this program
on the server. It calls exactly what the app's **Tools › Import** and
**File › Open** call (`src/ui/ImportPipeline`, `src/import`), so the same
file and choices give the same part, byte for byte
(`tests/ui/ImportServerTest.cpp` checks this on every build).

Source: `src/app/bld_import_server.cpp`, `src/app/importserver/`.

## Running it

```
bld-import-server [limits] <request.json>    # one step; the result JSON goes to stdout
bld-import-server [limits] --sandbox-check   # what the lock-down blocks
bld-import-server --version                  # "bld-import-server 2.1.10"
```

`limits`: `--cpu <seconds>` (default 120), `--memory-mb <MB>` (address
space, default 4096), `--file-size-mb <MB>` (any one file written, default
256), `--open-files <n>` (default 256). Zero or junk is a usage error (exit 2),
never "no limit".

Exit status: 0 when the result says `"ok": true`, 1 when it says why not, 2 for
a usage error. Anything else (a signal, a limit hit) means the run failed; the
server says the import failed and deletes the job.

## Request

Written by the server into the job's folder; at most 1 MB.

```json
{
  "command": "analyse" | "convert",
  "input": "/data/import-jobs/<job>/in/My Station.io",
  "outDir": "/data/import-jobs/<job>/out",
  "libraries": {
    "parts": ["/parts"],
    "ldraw": "/data/import-libraries/ldraw/ldraw",
    "studioLdraw": "",
    "lddAssets": "/data/import-libraries/ldd",
    "lddLdrawXml": "/data/import-libraries/ldd/ldraw.xml"
  },
  "limits": { "maxInputBytes": 67108864, "maxEntries": 5000,
              "maxUncompressedBytes": 805306368, "maxRatio": 100 },
  "options": {
    "mode": "part" | "parts",
    "quarterTurns": 0, "align": "" | "bottom" | "box", "nudge": [0, 0],
    "drop": [0, 3],
    "name": "My Station", "designer": "", "designerUrl": "", "author": ""
  }
}
```

- `input` keeps the uploaded file's own name (the server makes it safe):
  the app suggests the part's name from it.
- `options` is for `convert` only, and is what the import preview offers:
  `mode` `part` makes one custom part (Tools › Import), `parts` places the
  parts one by one as **File › Open** does (`.ldr` and `.mpd` only).
  `quarterTurns` turns the part clockwise; `align` and `nudge` (studs,
  at most 64 either way) line it up with the stud grid; `drop` lists the
  connection points left unchecked, by their index in the analyse result
  after the turn (turning keeps their order); `name` is "Save as" (empty:
  as suggested); a `designerUrl` without `http(s)://` gets `https://`, and is
  dropped without a `designer`.

## Results

Every result has `ok`, `version` and `sandbox` (below). A failure has
`error` (a code) and `message` (English words).

**analyse**: `format` (`ldraw`, `studio`, `ldd`), `kindLabel`, and

- `part` (when the model can be one): `widthStuds`, `heightStuds`,
  `pxPerStud`, `contentStuds` {x, y, width, height}, `baseOnGrid`,
  `snapMargin` {left, top, right, bottom}, `connections` [{type, x, y,
  angle}], `stats` {ldrawResolved, lddRendered, unresolved}, `warnings`
  (the app's own words), `sprite` (`preview.png` in `outDir`); and beside
  it `title`, `designer`, `suggestedName`, `partKey`. When it can't be a
  part, `partError` says why.
- `layout` (`.ldr` / `.mpd`): `sheets`, `bricks`, `warnings`, `partlyRead`;
  or `layoutError`.

**convert**, `mode: part`: `partKey`, `displayName`, `xml` and `sprite`
(paths in `outDir`: `part/<key>.xml`, `part/<key>.png` or `.gif`, the
picture Send to Server sends), `spriteMime`, `widthStuds`, `heightStuds`,
`connections` (how many), `stats`, `warnings`. The XML carries an
`<ImportSource>` naming the job's folder: the server strips it, as it does
for parts sent from the app.

**convert**, `mode: parts`: `bbm` (`layout.bbm` in `outDir`), `sheets`,
`bricks`, `warnings`, `partlyRead`.

Error codes: `bad_request`, `unsupported_type`, `no_input`, `no_out_dir`,
`file_too_large`, `damaged_archive`, `too_many_entries`, `archive_too_big`,
`archive_ratio`, `unsafe_archive`, `nothing_to_import`, `mode_unavailable`,
`write_failed`.

## Archives

`.io` and `.lxf` files are zips. Before any reader sees one, its central
directory is checked (nothing is inflated): at most `maxEntries` entries,
at most `maxUncompressedBytes` in all as the headers declare them, no entry
over 1 MB that is more than `maxRatio` times its compressed size, and no
symbolic links, absolute names or `..` (`unsafe_archive`). The readers then
inflate into buffers of the declared size and check every CRC
(`src/import/zip/SafeZip`), so a header that lies fails. Studio's
encryption is undone only here, as in the app.

## Sandbox

Applied in `main()` before Qt starts and before the request or the
uploaded file is opened (`src/app/importserver/Sandbox.cpp`):

| What | How |
|---|---|
| CPU time | `RLIMIT_CPU` = `--cpu` (soft = hard: the kernel kills it) |
| Memory | `RLIMIT_AS` = `--memory-mb` (LDD's 500 MB `Assets.lif` is mapped, so 4 GB) |
| File size | `RLIMIT_FSIZE` = `--file-size-mb` |
| Open files | `RLIMIT_NOFILE` = `--open-files` |
| Core files | `RLIMIT_CORE` = 0 |
| Privileges | `PR_SET_NO_NEW_PRIVS` |
| Network | seccomp: `socket()` fails with `EACCES` for every family, so no connection can be made whatever the namespace. Also an empty network namespace (`unshare(CLONE_NEWUSER\|CLONE_NEWNET)`) where the host allows one; Docker's default profile doesn't, and `sandbox.netns` is then false |
| Programs | seccomp: `execve` and `execveat` fail |
| Processes | seccomp: `fork`, `vfork` and `clone` without `CLONE_THREAD` fail; `clone3` answers `ENOSYS` so threads are made with `clone`, whose flags it can see |
| The rest | seccomp: `ptrace`, `process_vm_readv/writev`, `mount`, `umount2`, `unshare`, `setns`, `chroot`, `pivot_root`, `bpf`, `perf_event_open`, keyrings, `userfaultfd`, `io_uring_setup`, module and kexec loading fail; another architecture or the x32 ABI kills the process |

seccomp is on for Linux x86_64 and aarch64; the result's `sandbox` says what
took (`rlimits`, `noNewPrivs`, `netns`, `seccomp`) and `--sandbox-check`
also tries a TCP socket, `execv` and `fork` (`socketBlocked`,
`execBlocked`, `forkBlocked`). The server refuses to run a converter whose
check doesn't block all three.

The server adds the wall-clock timeout, an empty environment (only
`QT_QPA_PLATFORM=offscreen`, `HOME` and `TMPDIR`, both the job's folder),
the job's own folder as the working directory, and deletes that folder
afterwards.

## The bundle

CI builds `bld-import-server-linux-x86_64.tar.gz` (nightly:
`bld-import-server-nightly-linux-x86_64.tar.gz`) with its SHA-256 beside it
(`packaging/import-server/bundle.sh`, reproducible) and attaches it to the
release. It holds the program, the Qt libraries it loads (Core, Gui,
Widgets, DBus and ICU) and only the `offscreen` platform plugin. The system
packages it needs on Debian are in `scripts/ci/import-server-image-deps.txt`;
`scripts/ci/import-server-smoke.sh` runs it on the web image's base with
only those, as the server does.

`compat/compat.json` › `importServer` says which version a server needs
(`minimum`) and which the web image installs (`pinned`).

## Fuzzing

`fuzz_import_server_ldraw`, `_studio` and `_ldd` run every step the server
runs (analyse, convert to a part with every preview choice, with and
without an LDraw library, and placing as parts for LDraw) on fuzzed files
(fuzz/README.md).
