# Catalog: content sources, download and install

**Status:** draft for review, 2026-10-22. Nothing here is built yet.

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

## Sources

Sources live in `catalog/sources.conf` under the RetroFE root, using the same
`key = value` style as the other `.conf` files:

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
```

Manage gets a **Sources** screen (a form) to add, enable/disable and remove
sources, so nobody has to edit this file by hand.

### Adapters

| `type` | Reads | Notes |
|--------|-------|-------|
| `index` | A catalog index file (below) over HTTP(S) or from disk | The native format. Everything else maps to it. |
| `folder` | A folder of package archives, each with a sidecar `<file>.package.json`, or just archives | With no sidecars, packages are guessed from file names (as `archiveorg` does). Good for LAN or USB. |
| `archiveorg` | `https://archive.org/metadata/<item>` file lists | The One Saucier logic as an adapter: file name → stem/version via a `naming` rule, size and MD5 from the metadata, downloads from `/download/<item>/<file>`. |
| `github` (later) | Release assets of a repository | Same idea; release notes become descriptions. |

Each adapter caches what it read in `catalog/cache/<source>/`, keeping
`ETag`/`Last-Modified` so refreshing is cheap and the Catalog works offline
with the last known list. One Saucier fetches live and depends on a mirror
when offline; caching every source fixes that.

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
| `kind` | `collection`, `layout`, `media`, `emulator`, `bundle`, `other`. Groups the Catalog and picks sensible targets. |
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
- **Resume** with HTTP ranges into `catalog/downloads/<id>-<version>.part`,
  using One Saucier's `plan_resume` classification (complete / resume / fresh
  / discard).
- **Free space check** before starting: archive size plus extracted size.
- **Retries** with exponential backoff (2 s doubling to 60 s, 5 tries).
- **Verify** size always, and the hash whenever the source gives one. A
  mismatch fails the job; it never installs.
- **Progress** goes into the model (`info:downloads`), so Catalog and Manage
  screens show it with ordinary form fields and a new `progress` field type.

## Install

- **Stage, then swap.** Extract to `catalog/staging/<id>/`, write a `.ready`
  marker, then move into place; on startup, finish or roll back any staged
  install (One Saucier's `recover_staged_installs`). A crash or power cut
  never leaves half a package.
- **Stay inside the target.** Every archive entry is checked to resolve under
  its target folder; `..`, absolute paths and links are rejected (zip-slip).
- **Back up what's overwritten** into `catalog/backups/<id>/<old version>/`,
  so an update can be undone.
- **Write the manifest** `catalog/installed/<id>.json`: source, version, and
  every file written with its size and hash. One Saucier's docs list as limits
  that old installs have no file list and that user content is detected by
  markers; installing from day one with manifests avoids both.
- **Then reload** RetroFE, as Manage already does after creating a collection.

Formats: ZIP (including ZIP64) through the minizip already in the tree.
7-Zip would need another library; see the open questions.

Updates, uninstall and repair all come from the manifest: replace the files
it lists, delete them, or re-check their hashes. Files a user changed after
installing are kept, not overwritten silently.

## Screens

Lists are layout pages and actions are forms, as for the rest of the Home menu:

- **Catalog** is a list of packages, grouped by `kind`, built in memory like
  Home. Themes style it as the `Catalog` collection, and package artwork
  shows like collection artwork. Selecting a package opens its **details
  form**: description, size, version (installed and available), what it
  requires, and Install / Update / Remove buttons.
- **Manage → Installed** lists installed packages, with updates available
  first.
- **Manage → Sources** is the sources form above.
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
- Packages can only write under their targets (see Install).

## Libraries

- **HTTP:** libcurl, as One Saucier uses. Windows needs it vendored next to
  the other DLLs. (GStreamer's bundle has its own HTTP stack, but it isn't
  meant for this.)
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
   a URL or a USB stick.
2. **The `archiveorg` adapter,** so OnesaUCE content can come through the same
   Catalog.
3. **Updates, uninstall, repair, and requires** in Manage.
4. Later: GitHub releases, more archive formats, multiple downloads at once.

## Open questions

1. **Index format: JSON or XML?** JSON is proposed because it's what the web
   and archive.org use. XML would match RetroFE's layouts and wouldn't need a
   new parser for the native format (the archive.org adapter still needs JSON).
2. **Where is the `catalog/` folder?** Under the RetroFE root as proposed, or
   somewhere configurable (`catalogPath`), e.g. on a bigger drive than the
   install?
3. **7-Zip support in phase 1?** Many retro packs are `.7z`. It needs another
   library (libarchive or the 7-Zip SDK).
4. **Should a source be able to recommend other sources** (a "featured"
   list), or should every source be added by hand?
5. **Emulators as packages:** do we want `emulator` packages in phase 1, or
   start with collections and layouts and point at emulators the user
   already has?
6. **Should OnesaUCE content keep its `version.txt` markers** when installed
   through the `archiveorg` adapter, so One Saucier still recognises it on the
   same drive?
