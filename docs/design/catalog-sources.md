# Catalog: content sources, download and install

**Status:** draft, 2026-10-22. Reviewed; see [Decisions](#decisions).
What's built so far is marked in [Phases](#phases).

The Catalog is the Home menu's place for getting content: browse what's
available, install it, and keep it up to date. This document proposes how
content is described and where it comes from, so that the Catalog isn't tied
to archive.org, and how downloads and installs work. It borrows heavily from
One Saucier, which already does this for OnesaUCE on archive.org, and fixes
the limits its own docs call out.

## Goals

- **Any source.** An HTTP(S) URL, a folder on a NAS or USB stick, archive.org,
  and later things like GitHub releases. All look the same once loaded.
- **Describe content explicitly.** What a package is, what it installs, its
  version, its size and hash, what it needs, its artwork. Not inferred from
  file names.
- **Installs are reversible and checkable.** Every install records exactly
  which files it wrote, so it can be updated, verified or removed cleanly.
- **Works with any install layout.** A stock single-root RetroFE, CoinOPS, or
  a split OnesaUCE-style install (`baseItemPath`, `baseMediaPath`).
- **Controller-first and themeable,** like the rest of the Home menu.

## Non-goals

- RetroFE ships no sources and hosts no content. Users add sources
  themselves, the way a media player adds feeds.
- Publishing tools (uploading packages, building indexes). Those belong in a
  separate tool, as OnesaUCE Admin is for One Saucier.
- Running scripts that come with packages (see [Trust and safety](#trust-and-safety)).

## Concepts

```
 Source ──adapter──▶ Packages ──▶ Catalog screen ──▶ Download ──▶ Install ──▶ Manifest
 (where)             (what)        (browse/pick)      (fetch,      (stage,      (what was
                                                       verify)      move)        written)
```

- **Source:** somewhere packages come from, configured by the user.
- **Adapter:** turns one kind of source into packages. Everything after the
  adapter is the same for every source.
- **Package:** one installable unit with a version.
- **Manifest:** written for every installed package. Lists the files it put
  where, so nothing has to be guessed later.

## The catalog folder

Everything the Catalog keeps (sources, caches, downloads, staging, backups,
manifests) lives in one folder, set by `catalogPath` in `settings.conf`. It
defaults to `catalog` under the RetroFE root; relative paths are resolved
against the root like other paths. Point it at a bigger drive to keep
downloads off the install's drive. Paths below starting with `catalog/` mean
this folder.

## Sources

Sources live in `catalog/sources.conf`, using the same `key = value` style as
the other `.conf` files:

```ini
# One block per source: sources.<name>.<setting>
sources.community.type = index
sources.community.url = https://example.org/retrofe/catalog.json
sources.community.enabled = yes

sources.nas.type = folder
sources.nas.path = \\nas\retro\packages

sources.onesauce.type = archiveorg
sources.onesauce.items = onesaucev2system-game-packs, OnesaUCEv2BaseBuild
sources.onesauce.naming = onesauce     # "<stem> v<maj>.<min>b<beta>.zip"

sources.retrofe.type = github
sources.retrofe.repo = ennisj/RetroFE
sources.retrofe.prerelease = no        # yes also offers pre-releases
```

Manage gets a **Sources** screen (a form) to add, enable/disable and remove
sources, so nobody has to edit this file by hand.

### Adapters

| `type` | Reads | Notes |
|--------|-------|-------|
| `index` | A catalog index file (below) over HTTP(S) or from disk | The native format. Everything else maps to it. |
| `folder` | A folder of package archives, each with a sidecar `<file>.package.json`, or just archives | With no sidecars, packages are guessed from file names (as `archiveorg` does). Good for LAN or USB. |
| `archiveorg` | `https://archive.org/metadata/<item>` file lists | The One Saucier logic as an adapter: file name → stem/version via a `naming` rule, size and MD5 from the metadata, downloads from `/download/<item>/<file>`. With `naming = onesauce` it also writes the OnesaUCE `version.txt` markers on install, so One Saucier recognises the content on the same drive. |
| `github` | `https://api.github.com/repos/<owner>/<repo>/releases` | Offers RetroFE itself, as a package of kind `app` (see [GitHub sources and updating RetroFE](#github-sources-and-updating-retrofe)). Release notes become the description. |

Each adapter caches what it read in `catalog/cache/<source>/`, keeping
`ETag`/`Last-Modified` so refreshing is cheap and the Catalog works offline
with the last known list. One Saucier fetches live and depends on a mirror
when offline; caching every source fixes that. (Built so far: index sources
keep the last copy; GitHub sources also keep the `ETag`.)

## Catalog index format

A JSON document (the format web hosting and archive.org already speak):

```json
{
  "format": 1,
  "name": "Community packs",
  "packages": [
    {
      "id": "sega-genesis",
      "name": "Sega Genesis",
      "kind": "collection",
      "version": "2.1.0",
      "description": "Collection settings, launcher and artwork for Genesis.",
      "artwork": { "logo": "art/sega-genesis-logo.png", "screenshot": "art/sega-genesis.png" },
      "requires": [ { "id": "retroarch-genesis-plus-gx", "version": ">=1.0" } ],
      "files": [
        {
          "url": "packs/sega-genesis-2.1.0.zip",
          "size": 48211337,
          "sha256": "9f2c…",
          "target": "collections",
          "path": "Sega Genesis"
        }
      ]
    }
  ]
}
```

| Field | Meaning |
|-------|---------|
| `id` | Stable identifier, unique within the source. The install key (One Saucier's `stem`). |
| `name`, `description` | Shown in the Catalog. |
| `kind` | `collection`, `layout`, `media`, `bundle`, `other`; `emulator` is reserved for later. Groups the Catalog and picks sensible targets. `app` (RetroFE itself) only comes from `github` sources: an index or folder offering it, or the id `retrofe`, is skipped with a warning. |
| `version` | Compared to the installed one to offer updates. Semantic versions, plus the OnesaUCE `v2.0b13` style via the adapter. |
| `artwork` | Image URLs, relative to the index. Shown like collection artwork. |
| `requires` | Other packages, by `id` and optional version range. Offered together when installing. |
| `files[].url` | Archive or single file, relative to the index or absolute. |
| `files[].size`, `sha256` / `md5` | Checked after download. Archive.org gives MD5. |
| `files[].target` | Which root it installs into (below). |
| `files[].path` | Folder under the target to extract into. Empty means the target itself. |

### Install targets

Packages name a target, never a full path, and RetroFE resolves the target
against the install's own settings. That's what makes the same package work
in a stock install and in a split OnesaUCE-style one.

| `target` | Resolves to |
|----------|-------------|
| `root` | The RetroFE root (`Configuration::absolutePath`) |
| `collections` | `<root>/collections` (collection definitions) |
| `items` | `baseItemPath` if set, otherwise `<root>/collections`: ROM folders |
| `media` | `baseMediaPath` if set, otherwise `<root>/collections`: artwork |
| `layouts` | `<root>/layouts` |
| `meta` | `<root>/meta` |

## Download

The One Saucier pipeline, which works, moved behind the adapters:

- **Queue** of jobs. One HTTP connection per job; a couple of jobs at a time.
  One Saucier removed segmented downloads, and so do we.
- **Resume** with HTTP ranges into `catalog/downloads/<id>/<n>-<key>.part`,
  using One Saucier's `plan_resume` classification (complete / resume / fresh
  / discard). The key changes with the file's URL, size and hash, so a partial
  is only continued from the same file. A continued download that fails its
  hash is downloaded again from the start once.
- **Free space check** before starting: archive size plus extracted size
  (estimated as the archive size again) plus 64 MB.
- **Folder sources** are installed from where they are, not copied first.
- **Retries** with exponential backoff (2 s doubling to 60 s, 5 tries).
- **Verify** size always, and the hash whenever the source gives one. A
  mismatch fails the job; it never installs.
- **Progress** goes into the model, so Catalog and Manage screens show it with
  ordinary form fields (`package:progress`, `info:catalogStatus`). A
  `progress` bar field type is still to come.

Phase 1 runs one job at a time (a queue on one worker thread), not a couple at
once, and has no separate Downloads screen yet: each package's screen shows its
own progress and a Stop button, which keeps the partial download.

## Install

- **Stage, then swap.** Extract to `catalog/staging/<id>/`, write a `.ready`
  marker, then move into place; on startup, finish or roll back any staged
  install (One Saucier's `recover_staged_installs`). A crash or power cut
  never leaves half a package.
- **Stay inside the target.** Every archive entry is checked to resolve under
  its target folder; `..`, absolute paths and links are rejected (zip-slip).
- **Back up what's overwritten** into `catalog/backups/<id>/<name>/` before
  the first file moves, and record which destinations already held a file in
  a journal (`staging/<id>/commit.json`). A failed move, or a recovery that
  can't finish, puts every destination back from the journal and drops the
  staged install instead of retrying it on every start. Each manifest entry
  remembers the backup of the user's own file it replaced (carried through
  updates), so removal can put it back. Kept per package: those originals and
  the last update's backup.
- **Write the manifest** `catalog/installed/<id>.json`: source, version, and
  every file written with its size and hash. One Saucier's docs list as limits
  that old installs have no file list and that user content is detected by
  markers; installing from day one with manifests avoids both.
- **Then reload** RetroFE, as Manage already does after creating a collection.
  So as not to interrupt browsing the Catalog, the reload waits until the user
  is back on Home and nothing is queued. Collections installed into
  `collections/` are added to the Main menu first (and removed from it when
  their folder is gone), and a bare install gets its `settings.conf`.

Formats: ZIP (including ZIP64) through the minizip already in the tree.
7-Zip is left out for now; it would need another library.

Updates, uninstall and repair all come from the manifest: replace the files
it lists, delete them, or re-check their hashes. Files a user changed after
installing are kept, not overwritten silently.

## GitHub sources and updating RetroFE

*Built.* A `github` source turns a repository's releases into one package:
RetroFE itself.

```ini
sources.retrofe.type = github
sources.retrofe.repo = ennisj/RetroFE    # owner/name
sources.retrofe.app = yes                # may offer RetroFE (needed unless it's the official repository)
sources.retrofe.prerelease = yes         # optional: offer pre-releases too
sources.retrofe.url = https://...        # optional: another releases API address (GitHub Enterprise, tests)
```

**Adding it.** Manage → Sources has **Add RetroFE from GitHub**: one button,
after the same "I trust this source" confirmation as any source, adding a
source `retrofe` for `ennisj/RetroFE` with `app = yes`. The repository is the
constant `Catalog::retrofeRepository` in `Catalog/GitHub.h`. The ordinary form
adds any repository with the type "GitHub releases", taking `owner/name` or the
repository's github.com address.

**Which repositories may offer RetroFE.** Only the official one
(`retrofeRepository`), or one whose source says `app = yes`. Any other GitHub
source offers nothing and says why, with the line to add to
`catalog/sources.conf`. The package is named after where it's from:
"RetroFE from ennisj/RetroFE".

**Reading releases.** The adapter fetches
`https://api.github.com/repos/<repo>/releases?per_page=30` with
`Accept: application/vnd.github+json` (WinHTTP sends `User-Agent: RetroFE`).
Every field is read type-checked, so a malformed or hostile answer is skipped
rather than failing. Of the releases that aren't drafts, and aren't
pre-releases unless the source allows them, it takes the **highest version
that has an asset for this system**. That becomes the package:

| Field | From |
|-------|------|
| `id`, `kind` | `retrofe`, `app` |
| `name` | "RetroFE from `<repo>`" |
| `version` | The tag without a leading `v` (`v2504.3` → `2504.3`); tags that aren't versions are left out, with one warning listing them |
| `description` | The release notes, trimmed (at most 1500 characters) |
| `files[0]` | The asset's `browser_download_url` and `size`; `sha256` from its `digest` (`sha256:…`), which GitHub records for uploads, or else from `<asset>.sha256` beside it, fetched when installing |

**A checksum is required.** RetroFE won't install a release of itself it
can't check: without a digest or a `.sha256`, the package screen says "This
release has no checksum, so RetroFE won't install it", and the Catalog refuses
it. The release workflow uploads both. A `.sha256` file may be in any of the
usual forms: `<hex>`, sha256sum's `<hex>  <name>` or `<hex> *<name>`,
`<name> <hex>`, or BSD's `SHA256 (<name>) = <hex>`; if it lists several
files, the line naming the release file counts. The checksum only guards
against a damaged download: it comes from the same place as the file, so what
you're trusting is the repository you added.

**Asset naming.** `RetroFE-<version>-<os>-<arch>.zip`, e.g.
`RetroFE-2504.3-windows-x64.zip`. An asset is this system's if it's a `.zip`
whose name (split at `-`, `_`, `.` and spaces) includes the system
(`windows`/`win`/`win64`, `linux`, `macos`/`osx`/`mac`/`darwin`) and either
this architecture (`x64`/`x86_64`/`amd64`, `arm64`/`aarch64`, `x86`) or none,
and isn't symbols, a debug build or sources (`symbols`, `pdb`, `debug`, `dbg`,
`src`, `source`). Naming the architecture beats naming none; of equals, the
shortest name wins. Assets still uploading are ignored.

**Archive layout.** `retrofe/` holds the whole program folder (`retrofe.exe`,
its DLLs, `OpenSans.ttf`…), and `layouts/<name>/` optionally holds layouts
shipped with it. Anything else at the top, such as `RetroFE.lnk` for new
installs, is left out when updating. Names ending in `.old`, `.oldN`, `.new`,
`.failed` or `.part` are refused, since updates use them.
`.github/workflows/release-windows.yml` builds exactly this when a `v*` tag is
pushed, plus `<asset>.sha256`.

**Rate limits.** GitHub allows 60 unauthenticated requests an hour per
address. The list is cached in `catalog/cache/<source>/releases.json` with its
`ETag` (`state.json`), and:
- it isn't fetched again within a minute (RetroFE reloads re-read sources);
- later fetches send `If-None-Match`, and a `304` costs nothing against the limit;
- on `403` with `X-RateLimit-Remaining: 0`, or `429`, it waits until
  `X-RateLimit-Reset` (or `Retry-After`; 15 minutes if neither, at most 2
  hours) without asking again, showing the last copy and the warning
  "GitHub's limit of 60 checks an hour has been reached; RetroFE checks again
  after HH:MM";
- offline, it shows the last copy, as index sources do.

**What "installed" means for RetroFE.** The installed version is the running
build's (`RETROFE_VERSION_DESCRIBE`). A release build sets it to its tag with
`-DRETROFE_RELEASE_VERSION`; other builds report `RETROFE_VERSION_MAJOR.MINOR`
from `RetroFE/CMake/Versioning.cmake` (2504.2 today), so a development build
is offered any release tagged higher than that. Bump those numbers, or tag
releases above them, to keep development builds from being offered an older
release. The Catalog never installs a version that isn't newer than the
running one, and after an update it shows the new version as installed until
the restart, so the same update isn't offered twice. Any allowed GitHub source
may update it (it can't be removed to switch sources, unlike other packages).

### Updating RetroFE in place

Windows can't overwrite a running exe or a loaded DLL, nor replace one in a
single step (checked on Windows 11: `MoveFileEx` with replace, and a
POSIX-semantics rename, both fail on a running exe). It can rename them, and
it can rename the folder holding a running exe, as long as nothing has a file
in it open without delete sharing and it isn't any process's working folder.
`Catalog/SelfUpdate.cpp` uses both:

1. **Download and verify** as for any package (size, and the SHA-256).
2. **Extract and check** into `catalog/update/staged/`. `retrofe/<path>` maps
   to the program folder, `layouts/<name>/<path>` to `<root>/layouts/<name>/`,
   nothing else is installed. Refused outright: any path that could escape
   (the usual checks), programs or scripts in a layout, names updates use for
   themselves, and an archive without `retrofe/<the running exe's name>` (so
   a package for another system, or not RetroFE at all, is never applied).
   **Stop** works until this step and the next are done.
3. **Back up** every file about to be replaced or removed into
   `catalog/backups/retrofe/<running version>/`, at the same paths under the
   root, each checked against its SHA-256. With the unchanged files still in
   place, that's the whole previous version. Only the last update's backup is
   kept.
4. **Journal first.** `<root>/retrofe.update.json` (next to the program
   folder, so it's found before anything is configured, and independent of
   where the catalog is) is written, in state *preparing*, before anything is
   created. It names every file and folder the update will create (`<file>.new`,
   `retrofe.new`) or set aside (`<file>.old`, `retrofe.old`), with the SHA-256
   of each file placed and replaced. Only names it records are ever deleted.
   A `<file>.old` or `retrofe.old` that's already there and that an update
   didn't record (someone's own backup, say) is left alone, and the update
   uses `.old2` and so on instead. The journal is flushed to the disk before
   it replaces the previous one, so a power cut can't leave it empty.
5. **Prepare everything beside where it goes**, so the swap itself is renames
   only, even when the catalog is on another drive:
   - `<root>/retrofe.new/` becomes the complete new program folder: the
     release's files, plus every other file of the current folder, the
     user's own included (hard-linked, so it's instant and takes no space;
     copied if linking fails). Files the previous version installed that this
     one doesn't have, and haven't been changed, are left out. If the folder
     holds a link or junction, it isn't copied; the files are swapped one by
     one instead.
   - each changed layout file gets `<file>.new` beside it.
   If RetroFE stops while preparing, the next start deletes what the journal
   says was being made and nothing else. Then the journal moves to *swapping*,
   and `<root>/retrofe-UPDATE-INTERRUPTED.txt` tells anyone looking how to get
   RetroFE back by hand.
6. **Swap**, renames only, back to back:
   - layout files: `<file>` → `<file>.old`, `<file>.new` → `<file>`;
   - then the program folder: `retrofe` → `retrofe.old`, `retrofe.new` →
     `retrofe`. That second rename is the commit point. RetroFE first moves
     its own working folder to the root if it's inside `retrofe\` (the
     restarted RetroFE gets the original one back).
   - Each folder rename is tried for about two seconds (10 × 200 ms), since a
     virus scanner looking at the new files can hold it up. If `retrofe.new`
     won't go in, `retrofe.old` goes back; if that won't either, `retrofe.new`
     gets another try. If neither will, there's no program folder. The
     Catalog keeps trying every two seconds while RetroFE runs, RetroFE tries
     again as it exits, and if there's still none, it shows a message (it
     waits for OK) pointing at the note before exiting. The next start tries
     too.
   - If the program folder can't be renamed (another program has a file in it
     open), the program's changed files are swapped one by one the same way
     instead, removals first and the program file last. That's two renames
     per changed file, with nothing copied in between.
   - If any other rename fails, every step so far is undone and the message
     says nothing was changed.
7. **Record.** The journal is marked swapped, `catalog/installed/retrofe.json`
   lists the version's files, and the journal keeps the list of what was set
   aside (`retrofe.old`, or the `.old` files). What isn't in use is deleted at
   once.
8. **Restart.** The Service asks for a restart; RetroFE leaves its main loop
   (without running `exit.bat`, since it isn't really exiting), shuts
   everything down, and `main()` starts the program again with the same
   command line and working folder.
9. **Next start.** The Catalog's start-up job deletes the rest of what was set
   aside, retrying for about five seconds while the old process lets go of
   it, then removes the journal.

**If RetroFE stops part way** (a crash or power cut), the very first thing
`main()` does on the next start, before configuration, logging, SDL or
GStreamer, is look for `<root>/retrofe.update.json`:
- *Preparing*: what was being made is deleted, and that's all.
- *Swapping*, with the new program folder in place (the commit point was
  reached): the update is finished.
- *Swapping*, stopped between the two folder renames: `retrofe.old` goes back,
  or if it can't, `retrofe.new` goes in, and the update is finished.
- Otherwise every step is undone, last first. A file is only deleted or put
  back if its SHA-256 is what the journal says the update placed or replaced,
  so a damaged or tampered journal can't delete or swap anything else. All
  hashing opens files by their real names, so a RetroFE folder whose name
  isn't in the system code page works too. If program files were put back,
  RetroFE starts again at once so they're what runs. If it can't start
  again, it says so (a message that closes itself after a minute) and exits
  rather than running a mix.
- The journal may only name files in the program folder and under `layouts/`,
  under the names updates use, and its spare folders must be real
  `retrofe.new`/`retrofe.old…` folders, not links or junctions. Anything else,
  or a journal that can't be read, is set aside as
  `retrofe.update.damaged.json` and logged. Since RetroFE did start, nobody
  is told to restore anything.
- It tries at most three starts; pressing Install meanwhile doesn't use the
  tries up. After the third, the journal is set aside the same way, and the
  message saying where the backup is goes to the log and to the Catalog's
  **Problems** list (from `retrofe.update.damaged.txt`) until an update
  works. It isn't a message box at start, which would hold up a cabinet
  without a keyboard. Updates work again.
- Leftovers are only deleted if the journal lists them and they're real
  `retrofe.old…` (or unused `retrofe.new`) folders in the root, or files named
  `<name>.old`, `.oldN` or `.failed` in RetroFE's folders. Deleting never
  follows a link or junction.
- Only `<root>\retrofe\` is ever updated or recovered: a flat install, with
  the exe in the root, is left alone.

**What a power cut can still do.** With the folder swap, the program folder is
never a mix of versions: between the two folder renames it's briefly missing
(`retrofe.old` and `retrofe.new` both exist). Windows has no way to make the
pair atomic. If the power goes in that instant, `retrofe.exe` isn't where the
shortcut points, so nothing of RetroFE's can run to fix it. The note in the
root says what to do: rename `retrofe.old` back to `retrofe` (or `retrofe.new`
to `retrofe`, for the new version). In the
file-by-file fallback, a cut between a file's two renames leaves that one file
missing. If it's a DLL `retrofe.exe` loads at start (SDL2, GStreamer and the
other import-table DLLs), Windows can't start RetroFE, so again only the note
and the backup help. Otherwise the next start undoes it. The window is the
time between two renames, not the update's length.

User content (`collections/`, `settings*.conf`, `controls.conf`, `launchers*`,
`meta/`, the catalog's own records) is never touched. Self-update is refused
unless the program is in a folder of its own directly under the root
(`<root>/retrofe/` on Windows), so on an install where the exe sits in the root
it can't reach anything else.

**Why swap in-process and restart, rather than at the next start?** At any
start the exe and its DLLs are already loaded, so applying "at the next
start" would still need renames, plus a way to stop RetroFE from running a
half-updated program in between. Swapping right after the download, then
restarting at once, keeps old code running beside new files only until the
restart. The alternative, a separate updater program that waits for RetroFE
to exit and then copies, avoids in-use files entirely but is one more program
to ship and to update.

**Restoring the previous version by hand:** if `retrofe.old` exists, rename it
back to `retrofe`; otherwise copy `catalog/backups/retrofe/<version>/` over the
root.

## Screens

Lists are layout pages and actions are forms, as for the rest of the Home menu:

- **Catalog** is a list of packages, grouped by `kind`, built in memory like
  Home. Themes style it as the `Catalog` collection, and package artwork
  shows like collection artwork. Selecting a package opens its **details
  form**: description, size, version (installed and available), what it
  requires, and Install / Update / Remove buttons, Back to the Catalog, and
  Show it in Browse once a collection is installed (it reloads, then opens
  the collection).
- **Manage** lists installed packages; each opens its details form, also
  one no source offers any more (it can only be removed).
- **Manage → Sources** is the sources form above: a row per source (what it
  offers, or what's wrong) opening the source's own screen (type, address,
  when it was last read and how, its problems, its packages, Check for
  updates, Remove this source). Once a new source has been read, "Open the
  Catalog" shows the Catalog list. `Service::sourceStatus` keeps how each
  source's last reading went.
- **Downloads** (from either) shows the queue with progress, pause and cancel.

## Trust and safety

- HTTPS by default; plain HTTP only if a source is explicitly marked as such
  (e.g. on a LAN).
- Adding a source asks for confirmation and says that RetroFE doesn't vet
  what sources provide.
- Hashes are checked whenever they're given; a source without hashes is
  marked as such in its details.
- Packages never run code on install. Emulators are installed as files;
  their launchers are plain `.conf` files the user can read.
- Packages can only write under their targets (see Install), never into
  the catalog folder or RetroFE's program folder, and phase 1 refuses
  programs and scripts outright (no emulator packages yet).
- The one exception is RetroFE itself (kind `app`). Only a `github` source
  the user added can offer it, and only for the official repository or with
  `app = yes`. It must have a SHA-256, be newer than the running version,
  contain the running program's file, and it may only write into the program
  folder and the layouts it ships (see [Updating RetroFE in place](#updating-retrofe-in-place)).
- JSON the Catalog reads (indexes, GitHub's answers, caches, manifests,
  journals) is read field by field with type checks, and every Catalog job
  runs inside a handler, so bad data becomes a warning, never a crash.
- Paths are checked for every system: no `..`, absolute paths, drives or
  streams (`:`), Windows device names, forbidden characters, or segments
  ending in a dot or space. Ids and source names get the same device-name check.
- Plain `http://` is allowed only for local addresses (localhost, single-label
  and `.local` names, private IPv4), and that applies to file and artwork URLs
  inside an index, not just the index itself.
- A package installed from one source is only updated by that source.
- Resumed downloads send `If-Range` with the server's ETag or Last-Modified,
  so a file that changed on the server starts again rather than being joined
  to the old partial (which matters when an index gives no hash).
- Artwork is fetched in its own job after installs, at most 5 MB per image.
- Layout forms can't pre-fill a source's address or the trust confirmation.

## Libraries

- **HTTP:** WinHTTP on Windows, which is part of Windows, so nothing extra
  ships. Linux and macOS will use libcurl, as One Saucier does; until then,
  folder sources work there and HTTP sources say they aren't supported yet.
  (GStreamer's bundle has its own HTTP stack, but it isn't meant for this.)
- **JSON:** nlohmann/json (MIT, single header). Archive.org's metadata is
  JSON anyway, so the `archiveorg` adapter needs a JSON parser regardless of
  the index format.
- **ZIP:** minizip, already in the tree.

## What carries over from One Saucier

Copied deliberately, with One Saucier itself unchanged:

- `download_plan.h`: `plan_resume`, `decide_space`, `backoff_delay_ms`
- The staging, `.ready` and recovery sequence
- The archive.org metadata parsing and `<stem> v<maj>.<min>b<beta>` naming,
  inside the `archiveorg` adapter
- Its group layout (Base build, Game packs, Videos, Themes) as a starting
  point for `kind`

## Phases

1. **Index and folder sources, download, install, manifests,** and the
   Catalog list and details screens. Enough to install a collection pack from
   a URL or a USB stick. *Built.* It also covers update and remove from the
   package screen, and checks `requires` (without installing requirements).
   Not yet: `ETag`/`Last-Modified` (indexes are fetched in full each time),
   the `progress` field type, a Downloads screen, and HTTP on Linux/macOS.
2. **The `archiveorg` adapter,** so OnesaUCE content can come through the same
   Catalog.
3. **Updates, uninstall, repair, and requires** in Manage.
4. **GitHub releases and updating RetroFE itself.** *Built* (the `github`
   adapter, `app` packages, the self-update and the release workflow).
5. Later: more archive formats, multiple downloads at once, GitHub releases
   carrying content packages (not just RetroFE).

## Decisions

1. **The index format is JSON.**
2. **The catalog folder is configurable** with `catalogPath`, defaulting to
   `catalog/` under the root.
3. **No 7-Zip for now.** ZIP only. *Superseded:* 7-Zip is read for emulator
   downloads ([emulator-downloads.md](emulator-downloads.md)).
4. **Sources can't suggest other sources, for now** (option A below). Every
   source is added by hand; suggestions can be revisited later.
5. **No emulator packages yet.** Phase 1 covers collections, layouts and
   media; emulators come later. *Superseded:* emulators from a built-in list are
   packages ([emulator-downloads.md](emulator-downloads.md)).
6. **OnesaUCE content keeps its `version.txt` markers** when installed
   through the `archiveorg` adapter.
7. **RetroFE updates itself only from `github` sources** (kind `app`, id
   `retrofe`): the official repository, or one whose source says `app = yes`.
   Index and folder sources can't offer it, and a release without a SHA-256
   isn't installed.
8. **Self-update swaps in-process, then restarts** (see
   [Updating RetroFE in place](#updating-retrofe-in-place)): the program
   folder is swapped whole with two renames, falling back to renaming files
   aside one by one, rather than at the next start or through a separate
   updater program. Interrupted updates are finished or undone first thing
   in `main()`.
9. **Release assets are named `RetroFE-<version>-<os>-<arch>.zip`** and hold
   `retrofe/` plus optional `layouts/<name>/`, with `<asset>.sha256` beside them.

## Background: could a source suggest other sources?

*Decided: option A for now. Kept here for when this is revisited.*

**The problem it solves.** Every source has to be added before the Catalog
shows anything, and on a cabinet that means typing a URL on the on-screen
keyboard. Letting a source point at others means a user types one URL and can add
the rest with a single press each.

**How it would work.** An index can list other sources next to its
packages:

```json
"sources": [
  {
    "name": "OnesaUCE packs",
    "type": "archiveorg",
    "items": ["onesaucev2system-game-packs"],
    "naming": "onesauce",
    "description": "Game packs for the OnesaUCE build."
  },
  {
    "name": "Theme makers",
    "type": "index",
    "url": "https://example.org/themes/catalog.json"
  }
]
```

The options:

| | Option | What happens | Trade-off |
|-|--------|--------------|-----------|
| A | **None** | Every source is added by hand. | Simplest and most explicit, but a lot of typing on a controller. |
| B | **Suggestions** (proposed) | Manage → Sources lists them under "Suggested by Community packs", each with an Add button that asks for the same confirmation as adding any source. Nothing is added or loaded on its own. | One deliberate press per source, and the user sees who suggested it. |
| C | **Follow automatically** | Suggested sources are loaded as if the user had added them. | No extra step, but trust becomes transitive: one source, or someone who takes it over, could quietly bring in others, and suggestion loops have to be handled. |

**Proposed: B,** with these rules:

- Suggestions are shown, never acted on. Adding one goes through the normal
  confirmation and it becomes an ordinary source the user can remove.
- Each suggestion shows which source suggested it.
- A suggestion can't change a source that's already added (e.g. repoint its
  URL); a matching suggestion just shows as "Added".
- Once added, a suggested source's own suggestions show too. Every step is a
  user's choice, so there's no automatic chain.

**A related way to avoid typing,** independent of the above and not yet
decided: when a USB stick or folder has a `sources.conf` (or an index file) at
its top level, Manage → Sources could offer "Add sources from E:\". That works
without any network and without suggestions. It isn't part of phase 1.
