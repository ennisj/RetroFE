# Emulator downloads

**Status:** built, 2026-11-27: the back end (`RetroFE/Source/Downloads`, namespace
`Downloads`) and the Manage screens (`Manager/Downloads`). Earlier rounds built this on
top of the Catalog (packages from configured sources, with a trust screen); the Catalog
is gone, and so are its sources, `sources.conf`, its package screens and RetroFE's
self-update. What's left is this list of emulators, checked, installed and removed from
the screens below. See [FOLDERS.md](../FOLDERS.md) for where the files go.

On a bare install, a system needs a launcher, and a launcher needs an emulator program.
Without downloads the program must already be on the drive. Manage can download and
install the major emulators, create their launchers, and update them. RetroArch also
needs cores, which come in a second step.

The order of setting up from nothing becomes: **launcher** (download an emulator, or point
at one) → **system** (a folder of games and its launcher or core) → **games**.

## What the user sees

The screens are `download-emulators`, `download-emulator` (param `id`, e.g.
`emulator-duckstation`), `download-cores` and `download-core` (param `core`), served by
`Manager::DownloadProvider` (prefix `download`); see [MANAGER.md](../MANAGER.md#downloading-emulators).

- **Launchers → Add a launcher** has a first row, **Download an emulator**. The bare
  install's hub link "Add your first launcher/emulator" opens the same Add a launcher form,
  so it's one Select away. **Add a system** has the same link right after its Launcher
  choice; Back from it, the choice includes what was installed (and picks it when it's
  the one new launcher), because the launcher write marks the library changed and the
  choice is read from the rebuilt index.
- **Download an emulator** lists the six emulators below with their state: "Not
  installed, 1.22.2 available", "Installed 1.22.1, 1.22.2 available", "Installed 1.22.2".
  Opening it lists what's installed at once (no network), then checks every entry's
  latest release in the background. **Versions are checked only then** (and with **Check
  again**): never when RetroFE starts or Manage opens, and not when coming Back to the
  screen. A note says when the check couldn't be made (offline; GitHub's limit, with the
  time to try again), and any warnings are listed. Where downloads aren't supported
  (anything but Windows x64) the screen only says "Emulator downloads are Windows-only for
  now."
- **An emulator's screen**: what it is, installed and available versions, download size,
  where it comes from (`github.com/<repo>`, or libretro's server for RetroArch, with
  "libretro doesn't publish a checksum for this download"), its launcher, progress while
  it runs ("Downloading 45 of 120 MB", "Installing...") and the last result. Pinned
  buttons: **Install**, **Update** (when there's a newer version), **Remove**, **Stop**
  (while it runs). RetroArch's screen has **Cores** once it's installed.
- **The trust step is the confirmation**, every time: Install and Update ask "Download
  DuckStation from github.com/stenzek/duckstation? RetroFE checks the published checksum,
  but not what it contains." (for RetroArch, that libretro publishes none). Nothing is
  stored: there's no trust setting and no source to add. The question names the source
  through `confirm="... {bind:download:source} ..."`, read when it's asked.
- Installing puts the emulator in `emulators/<name>/`, makes it portable where it can be,
  and creates its launcher (except RetroArch, whose launchers come with its cores). The
  message names the launcher. Leaving the screen stops a download (it's kept, so the next
  Install continues it).

## The emulators RetroFE knows

A built-in list (`Downloads/Emulators.cpp`), one entry per emulator. Windows x64 only for
now ([DECISIONS.md](../DECISIONS.md) W4); on other systems the screen only says so.

| Emulator | Releases from | Version from | Download | Program | Portable | Launcher arguments |
|---|---|---|---|---|---|---|
| RetroArch | GitHub `libretro/RetroArch` (tags) + libretro's server | tag `v1.22.2` | `https://buildbot.libretro.com/stable/<version>/windows/x86_64/RetroArch.7z`, top folder `RetroArch-Win64/` removed | `retroarch.exe` | by default | per core (below) |
| MAME | GitHub `mamedev/mame` | tag `mame0289` → `0.289` | `mame<NNNN>b_x64.exe`, a self-extracting 7-Zip archive | `mame.exe` | by default | `%ITEM_NAME% -rompath "%ITEM_DIRECTORY%."` |
| DuckStation | GitHub `stenzek/duckstation` | tag `v0.1-12083` → `0.1.12083` (not the rolling `latest`/`preview`) | `duckstation-windows-x64-release.zip` | `duckstation-qt-x64-ReleaseLTCG.exe` | `portable.txt` | `-batch -fullscreen -- "%ITEM_FILEPATH%"` |
| PCSX2 | GitHub `PCSX2/pcsx2`, releases only | tag `v2.8.2` | `pcsx2-v<version>-windows-x64-Qt.7z` | `pcsx2-qt.exe` | `portable.txt` | `-batch -fullscreen -- "%ITEM_FILEPATH%"` |
| PPSSPP | GitHub `hrydgard/ppsspp` | tag `v1.20.4` | `PPSSPP-v<version>-Windows-x64.zip` | `PPSSPPWindows64.exe` | by default | `--fullscreen "%ITEM_FILEPATH%"` |
| Flycast | GitHub `flyinghead/flycast` | tag `v2.7` | `flycast-win64-<version>.zip` | `flycast.exe` | by default | `"%ITEM_FILEPATH%"` |

Add a system's Launcher choice lists the global launchers, including core launchers,
then A program on the drive; the **Download an emulator** link under it opens the
download screens, and Back returns to the form, whose launcher list includes anything
just installed. The selected
launcher suggests Name and Extensions until the user types in those fields.

| Launcher | Suggested system | Extensions |
|---|---|---|
| DuckStation | PlayStation | `cue,chd,bin,img,iso,m3u,pbp,ecm,mds,psexe,psf,minipsf` |
| PCSX2 | PlayStation 2 | `iso,chd,cso,zso,gz,elf,irx,m3u,cue,bin,img,mdf,nrg,dump` |
| PPSSPP | PSP | `iso,cso,chd,pbp,elf,prx` |
| Flycast | Dreamcast | `chd,cdi,gdi,cue,m3u,elf,dat,zip,7z,lst` |
| MAME | Arcade | `zip,7z,chd` |
| RetroArch core installed by Manage | Its info file's `systemname` | Its `supported_extensions`, with pipes changed to commas |

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

## How a release is found

Each entry's releases are read from GitHub's API (`/repos/<repo>/releases`), cached in
`manage/downloads/cache/<entry>/` with their `ETag`: a `304` costs nothing against
GitHub's 60 checks an hour, a list is asked for at most once a minute, and when the limit
is reached no entry asks again until GitHub's reset time (`cache/rate-limit.json`, shared).
When GitHub can't be asked, the last copy is shown with a warning. The highest version by
the entry's tag rule wins, never a draft or pre-release; its asset needs GitHub's
SHA-256 `digest`. RetroArch's version comes from its tags, its download from libretro's
stable builds. The download is only ever from the entry's own origin
(`https://github.com/<repo>/releases/download/`, or libretro's `RetroArch.7z`), decided in
code (`Downloads::checkPackage`), never by anything downloaded.

## Checksums

GitHub records a SHA-256 for every release asset, and RetroFE requires it for GitHub
downloads. Libretro's server publishes none for
`RetroArch.7z`, and only a CRC-32 per core (checked against the unpacked DLL). Those are allowed for libretro's server only,
over HTTPS, and RetroArch's screen says "libretro doesn't publish a checksum for this
download". The 7-Zip and zip formats check their own CRCs as they're unpacked, which guards
against a damaged download, not a tampered one.

## Installing

Downloads go to `manage/downloads/partial/`, and are continued after a stop or a lost
connection. The installer (`Downloads::Installer`) unpacks into
`manage/downloads/staging/<id>/`, then moves the files into place, recording them in
`manage/downloads/installed/<id>.json`; files it replaces are kept in
`manage/downloads/backups/<id>/`. An install interrupted part way is finished or put back
the next time a download screen is used (never at RetroFE's start). Also:

- **Archives:** `.zip` (minizip), `.7z`, and 7-Zip self-extracting `.exe` files, read with
  the LZMA SDK's C decoder (public domain, in `RetroFE/ThirdParty/lzma-sdk`). A
  self-extracting archive is opened by finding the 7-Zip signature after the program stub;
  **nothing downloaded is ever run.** The usual path checks apply to every name inside.
  RetroArch's `.7z` uses LZMA2, LZMA and BCJ2; PCSX2's and MAME's use LZMA2. All are in
  the SDK's decoder.
- **Programs are allowed** only for entries of the built-in list, and only under
  `emulators/<folder>/`. A core may write only its own `RetroArch/cores/<core>_libretro.dll`.
- **The folder must be the installer's.** If `emulators/<folder>/` exists and wasn't
  installed by RetroFE, the install stops: "emulators/MAME is already there. Rename it,
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
- RetroArch's screen has **Cores**, and Download an emulator has **RetroArch cores**
  (once RetroArch is installed): a list with a System choice, installed ones first. Each
  core opens its own screen with Install, Update, Remove and Stop, like an emulator's.
- Installing a core creates the launcher `RetroArch <core name>` (e.g. "RetroArch Genesis
  Plus GX") with `-L "%RETROFE_PATH%\emulators\RetroArch\cores\<core>_libretro.dll"
  "%ITEM_FILEPATH%"`.
- The list is cached at `manage/downloads/cache/cores.index` with its Last-Modified value
  in `cores.modified`, and read when the cores screen opens. Requests send `If-Modified-Since`; 304 uses the cached list.
  RetroArch's installed manifest is required; otherwise the screen says
  "Install RetroArch first". Downloads remain Windows x64 only.
- The index CRC-32 belongs to the unpacked DLL, not the ZIP. Only core packages set
  `files[].crc32`. A core ZIP must have exactly one entry, the expected DLL,
  with that stored CRC; extraction also checks the data against the ZIP CRC.
- Core launchers use printable ASCII names, collapse spaces, remove launcher-forbidden
  characters and stop at 64 characters. Existing names stay. The manifest records
  created launchers; changed launchers and those used by systems or games stay on removal.
- RetroArch cannot be removed while core manifests remain. Removing a core leaves
  RetroArch alone. A newer nightly shows in the cores list and on the core's screen.

## The screens' jobs

`Downloads::Service`'s calls are synchronous, so `DownloadProvider` runs each (check,
install, remove, the core list, a core's install or removal) as a job on a worker thread,
one at a time, with a progress callback and a stop flag. The screens read what it
reports, under a lock, and `generation()` bumps so they redraw. Leaving the screen a job
was started on sets the stop flag and joins the thread (so does the provider's
destructor); opening another screen on top doesn't. When a job wrote or removed a launcher,
the provider calls `library->changed()` from `update()`, on the manager's thread, so
launcher lists refresh and RetroFE reloads when Manage closes. The service, and its Http,
are made the first time a download screen opens. Tests give the provider a fake Http and
say downloads are supported (`DownloadProvider::Hooks`), so they run on Linux.

## Decisions

1. **7-Zip archives are read**, with the LZMA SDK's C decoder, because RetroArch, PCSX2
   and MAME publish only 7-Zip.
2. **Only the built-in list is installed,** only into `emulators/`. Adding an emulator is
   adding an entry, reviewed like any other code.
3. **Libretro downloads may lack a SHA-256;** GitHub downloads may not.
4. **Trust is asked each time** in the Install and Update confirmation, which names the
   source; no trust setting or source list is kept.
5. **Versions are checked only when the screen opens** (or Check again), never at start.

## Backend notes

- Release parsing is pure and tested without HTTP. The GitHub reader keeps
  each entry's cache and ETag under `manage/downloads/cache/<id>/`.
- The installer checks each package against the built-in list at extraction,
  staging and commit. Package and index fields cannot grant program permission.
- Launcher creation and removal run in `Downloads::Service`, using the Library
  editor and a fresh Windows Library index. The launcher path and SHA-256 are
  recorded separately from archive files, so updates never rewrite it. A changed
  launcher or one used by a system or game stays; the result names its users.
- Portable files are recorded with their empty-file hash and retained across
  updates; removal keeps them if the emulator has written into them. Changed
  emulator program files are also kept during updates.
- The archive readers and portable arguments still need checking with real
  Windows releases. The tests (DownloadsTests, DownloadScreensTests) use release JSON
  and local archives only.
