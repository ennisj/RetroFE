# Emulator downloads

**Status:** emulator and core downloads built, 2026-10-29. Builds on [catalog-sources.md](catalog-sources.md) and
[FOLDERS.md](../FOLDERS.md).

On a bare install, a system needs a launcher, and a launcher needs an emulator program.
Without downloads the program must already be on the drive. Manage can download and
install the major emulators, create their launchers, and keep them up to date through the
Catalog. RetroArch also needs cores, which come in a second step.

The order of setting up from nothing becomes: **launcher** (download an emulator, or point
at one) → **system** (a folder of games and its launcher or core) → **games**.

## What the user sees

- **Launchers → Add a launcher** gains a first link, **Download an emulator**. The bare
  install's hub link "Add your first launcher/emulator" opens the same Add a launcher form,
  so it's one Select away.
- **Download an emulator** lists the emulators RetroFE knows (below), each with its
  installed and available versions. Picking one opens its package screen, the Catalog's
  usual one: what it is, its size, Install / Update / Remove.
- The first time, the screen asks the user to trust the downloads, like adding any source:
  "Emulator downloads come from each emulator's own GitHub releases, or libretro's server
  for RetroArch. RetroFE doesn't check what they contain." One toggle, one button. That
  adds the source `emulators` to `catalog/sources.conf`.
- Installing puts the emulator in `emulators/<name>/`, makes it portable where it can be,
  and creates its launcher (except RetroArch, whose launchers come with its cores). The
  message names the launcher, and focus goes to "Add your first system" on a bare install.
- Installed emulators also appear under "Installed from the Catalog" on the hub, and the
  Catalog offers their updates like any package.

## The emulators RetroFE knows

A built-in list (`Catalog/Emulators.cpp`), one entry per emulator. Windows x64 only for
now ([DECISIONS.md](../DECISIONS.md) W4); on other systems the screen says so and has no trust toggle.

| Emulator | Releases from | Version from | Download | Program | Portable | Launcher arguments |
|---|---|---|---|---|---|---|
| RetroArch | GitHub `libretro/RetroArch` (tags) + libretro's server | tag `v1.22.2` | `https://buildbot.libretro.com/stable/<version>/windows/x86_64/RetroArch.7z`, top folder `RetroArch-Win64/` removed | `retroarch.exe` | by default | per core (below) |
| MAME | GitHub `mamedev/mame` | tag `mame0289` → `0.289` | `mame<NNNN>b_x64.exe`, a self-extracting 7-Zip archive | `mame.exe` | by default | `%ITEM_NAME% -rompath "%ITEM_DIRECTORY%."` |
| DuckStation | GitHub `stenzek/duckstation` | tag `v0.1-12083` → `0.1.12083` (not the rolling `latest`/`preview`) | `duckstation-windows-x64-release.zip` | `duckstation-qt-x64-ReleaseLTCG.exe` | `portable.txt` | `-batch -fullscreen -- "%ITEM_FILEPATH%"` |
| PCSX2 | GitHub `PCSX2/pcsx2`, releases only | tag `v2.8.2` | `pcsx2-v<version>-windows-x64-Qt.7z` | `pcsx2-qt.exe` | `portable.txt` | `-batch -fullscreen -- "%ITEM_FILEPATH%"` |
| PPSSPP | GitHub `hrydgard/ppsspp` | tag `v1.20.4` | `PPSSPP-v<version>-Windows-x64.zip` | `PPSSPPWindows64.exe` | by default | `--fullscreen "%ITEM_FILEPATH%"` |
| Flycast | GitHub `flyinghead/flycast` | tag `v2.7` | `flycast-win64-<version>.zip` | `flycast.exe` | by default | `"%ITEM_FILEPATH%"` |

Add a system lists global launchers first, including core launchers, followed by
Download an emulator and A program on the drive. Downloads opens this screen; Back
returns to the form, whose launcher list includes anything just installed. The selected
launcher suggests Name and Extensions until the user types in those fields.

| Launcher | Suggested system | Extensions |
|---|---|---|
| DuckStation | PlayStation | `cue,chd,bin,img,iso,m3u,pbp,ecm,mds,psexe,psf,minipsf` |
| PCSX2 | PlayStation 2 | `iso,chd,cso,zso,gz,elf,irx,m3u,cue,bin,img,mdf,nrg,dump` |
| PPSSPP | PSP | `iso,cso,chd,pbp,elf,prx` |
| Flycast | Dreamcast | `chd,cdi,gdi,cue,m3u,elf,dat,zip,7z,lst` |
| MAME | Arcade | `zip,7z,chd` |
| RetroArch core installed by the Catalog | Its info file's `systemname` | Its `supported_extensions`, with pipes changed to commas |

These are file types the emulators accept, including executables and playlists;
a disc's track files may also be listed beside its cue sheet. References: [DuckStation's README](https://github.com/stenzek/duckstation/blob/master/README.md),
[PCSX2's disc guide](https://github.com/PCSX2/pcsx2-net-www/blob/main/docs/setup/discs.md),
[PPSSPP's game guide](https://www.ppsspp.org/docs/getting-started/how-to-get-games/),
[Flycast](https://github.com/flyinghead/flycast), and
[MAME's ROM sets](https://docs.mamedev.org/usingmame/aboutromsets.html).

Each entry says: id and display name, folder name under `emulators/`, where releases come
from, how a tag becomes a version (and which tags count), how to pick the asset, the
archive's form (zip, 7z or self-extracting 7z, and a top folder to remove), the program,
files to create for portable mode, the launcher's name and arguments, and the systems it
suits (for adding a system later). Adding an emulator is adding an entry; entries are code
reviewed like any other change.

The launcher arguments and portable files are what each emulator documents; each is a
VERIFY.md row until checked on Windows. Cemu, xemu, Mesen and ares publish zips the same
way and can follow. Dolphin and RPCS3 don't publish Windows builds on GitHub releases.

## The `emulators` source

```ini
sources.emulators.type = emulators
sources.emulators.enabled = yes
```

It has no address: it reads the built-in list, and for each entry the entry's releases.
GitHub releases are read as the `github` source reads them today (cached with `ETag`, a
`304` costs nothing against GitHub's 60 checks an hour, and the same waiting when the limit
is reached). It makes one package per emulator:

| Field | Value |
|---|---|
| `id` | `emulator-<id>` (e.g. `emulator-duckstation`) |
| `kind` | `emulator` |
| `version` | the highest release version, by the entry's rule; pre-releases never |
| `files[0]` | the asset (or libretro URL), `target = emulators`, `path = <folder>` |
| `sha256` | GitHub's `digest` for the asset |

## Checksums

GitHub records a SHA-256 for every release asset, and the Catalog requires it for GitHub
downloads, as for RetroFE's own updates. Libretro's server publishes none for
`RetroArch.7z`, and only a CRC-32 per core (checked against the unpacked DLL). Those are allowed for libretro's server only,
over HTTPS, and the package screen says "libretro doesn't publish a checksum for this
download". The 7-Zip and zip formats check their own CRCs as they're unpacked, which guards
against a damaged download, not a tampered one.

## Installing

Emulator packages unpack 7z and self-extracting 7z files by default, as well as
files published as `.zip`. A file can override this with `files[].extract`.

The Catalog's installer as it is (staging, `.ready`, recovery, manifest, backups), with:

- **Archives:** `.zip` (minizip), `.7z`, and 7-Zip self-extracting `.exe` files, read with
  the LZMA SDK's C decoder (public domain, in `RetroFE/ThirdParty/lzma-sdk`). A
  self-extracting archive is opened by finding the 7-Zip signature after the program stub;
  **nothing downloaded is ever run.** The usual path checks apply to every name inside.
  RetroArch's `.7z` uses LZMA2, LZMA and BCJ2; PCSX2's and MAME's use LZMA2. All are in
  the SDK's decoder.
- **Programs are allowed** for `emulator` packages from the `emulators` source, and only
  under `emulators/<folder>/`. Cores from that same configured source may write
  only their own `RetroArch/cores/<core>_libretro.dll`. Every other source still
  has programs and scripts refused.
- **The folder must be the Catalog's.** If `emulators/<folder>/` exists and wasn't
  installed by the Catalog, the install stops: "emulators/MAME is already there. Rename it,
  or add a launcher for it instead."
- **Portable files** the entry lists are created if missing (empty). They're recorded in
  the manifest as created, so removing the emulator removes them.
- **The launcher** `launchers.windows/<name>.conf` is written with `%RETROFE_PATH%` paths,
  as Manage writes launchers, unless a launcher of that name already exists (then it's left
  alone, and the message says so). It's recorded in the manifest as created.

Settings the emulator writes later (its `.ini`, `retroarch.cfg`, saves, BIOS files) aren't
in the manifest, so updates and removal never touch them, except that removal says which
files it leaves behind.

## Updating and removing

An update replaces every file shipped by the new version, including files changed by
an emulator's own updater. Changed files are backed up and the result names their count
and backup folder. Unchanged files dropped by the new version are removed; changed
dropped files stay. Files outside the installed manifest, including settings, saves and
BIOS files, are never touched.

Removing an emulator removes its files and the launcher it created, unless a system still
uses that launcher: then the launcher stays and the message names the systems.

## RetroArch cores (second step)

Cores come from libretro's nightly builds, one zip each:
`https://buildbot.libretro.com/nightly/windows/x86_64/latest/<core>_libretro.dll.zip`, listed
in `.index-extended` beside them (`<date> <crc32> <file>` per line, about 250 cores).

- Each core is a package of kind `core`, id `core-<core>`, version its date, `requires`
  `emulator-retroarch`, installed to `emulators/RetroArch/cores/`. Its name and the systems it
  runs come from RetroArch's own `info/<core>_libretro.info`
  (`corename`, falling back to the file stem; `systemname` for the filter,
  `display_name` for its description), installed with
  RetroArch.
- Cores aren't shown in the main Catalog list. RetroArch's installed package screen has **Cores**, and Download an emulator
  has **RetroArch cores**: a list filtered by system, with installed ones first.
- Installing a core creates the launcher `RetroArch <core name>` (e.g. "RetroArch Genesis
  Plus GX") with `-L "%RETROFE_PATH%\emulators\RetroArch\cores\<core>_libretro.dll"
  "%ITEM_FILEPATH%"`.
- The list is cached at `cache/<source>/cores.index` with its Last-Modified value
  in `cores.modified`. Requests send `If-Modified-Since`; 304 uses the cached list.
  RetroArch's installed manifest is required; otherwise the screen says
  "Install RetroArch first". Downloads remain Windows x64 only.
- The index CRC-32 belongs to the unpacked DLL, not the ZIP. Only this source sets
  `files[].crc32`. A core ZIP must have exactly one entry, the expected DLL,
  with that stored CRC; extraction also checks the data against the ZIP CRC.
- Core launchers use printable ASCII names, collapse spaces, remove launcher-forbidden
  characters and stop at 64 characters. Existing names stay. The manifest records
  created launchers; changed launchers and those used by systems or games stay on removal.
- RetroArch cannot be removed while core manifests remain. Removing a core leaves
  RetroArch alone. Core nightly updates appear only in Cores and their package screens,
  never as general Catalog updates. The installed package list still allows removal.
- Adding a system then offers launchers and cores together, filtered by the folder's file
  extensions (the next round).

## Steps

1. **Archives:** the LZMA SDK in ThirdParty, `.7z` and self-extracting `.7z` in the
   installer, with tests. **Built.**
2. **The `emulators` source and packages:** the built-in list, the source and its trust
   screen, the `emulators` target, programs allowed there, portable files, launcher
   creation, update and removal rules, and the Download an emulator screen.
   **Built.** Download an emulator includes the trust step and the built-in list.
   The model action is `source.addEmulators`, requiring `draft:emulatorsTrusted`;
   forms cannot pre-fill that trust toggle. `package:noChecksum` exposes the
   libretro checksum notice. No emulator source is added automatically.
3. **RetroArch cores. Built.**
4. **Adding a system** picks a launcher or a core (next round).

## Decisions

These change earlier Catalog decisions (catalog-sources.md):

1. **7-Zip archives are read** (was "ZIP only"), with the LZMA SDK's C decoder, because
   RetroArch, PCSX2 and MAME publish only 7-Zip.
2. **Emulators are packages** (was "no emulator packages yet"), only from the built-in
   list, only into `emulators/`.
3. **Libretro downloads may lack a SHA-256;** GitHub downloads may not.
4. **One `emulators` source with a built-in list,** rather than a GitHub source per
   emulator added by hand. The list knows each emulator's program, arguments and portable
   files, which a bare GitHub repository doesn't say.

## Backend notes

- Release parsing is pure and tested without HTTP. The shared GitHub reader keeps
  each entry's cache and ETag under `cache/<source>/<id>/`.
- The installer checks the configured source type at extraction, staging and
  commit. Package and index fields cannot grant program permission. Its manifest
  records that configured origin for removal after a source is removed.
- Launcher creation and removal run in the Catalog Service, using the Library
  editor and a fresh Windows Library index. The launcher path and SHA-256 are
  recorded separately from archive files, so updates never rewrite it. A changed
  launcher or one used by a system or game stays; the result names its users.
- Portable files are recorded with their empty-file hash and retained across
  updates; removal keeps them if the emulator has written into them. Changed
  emulator program files are also kept during updates.
- The archive readers and portable arguments still need checking with real
  Windows releases. This job's tests use release JSON and local archives only.
