# Emulator downloads

**Status:** built, 2026-11-27: the back end (`RetroFE/Source/Downloads`, namespace
`Downloads`) and the Manage screens (`Manager/Downloads`). Round 39 added Linux
(x86_64, arm64) and macOS: per-platform tables, libcurl, AppImages, disk images,
Unix permissions and links ([below](#linux-and-macos)); built and tested with
fake downloads on Linux, not yet tried with real ones on either system. Earlier rounds built this on
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
  time to try again), and any warnings are listed. An emulator with no build for this
  platform says "No Linux arm64 build" (the platform's name) in the list and on its screen,
  which has no Install or Update; nothing is asked of GitHub for it. Where there are no
  downloads at all (no Http, or a system other than Windows x64, Linux x86_64 or arm64 and
  macOS) the screen only says "Emulator downloads aren't available on this system."
- **An emulator's screen**: what it is, installed and available versions, download size,
  where it comes from (`github.com/<repo>`, or libretro's server for RetroArch, with
  "libretro doesn't publish a checksum for this download"), its launcher, progress while
  it runs ("Downloading 45 of 120 MB", "Installing...") and the last result. Pinned
  buttons: **Install**, **Update** (when there's a newer version), **Remove**, **Stop**
  (while it runs). RetroArch's screen has **Cores** once it's installed, where its
  cores are offered (not Linux arm64, which has no RetroArch).
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

A built-in list (`Downloads/Emulators.cpp`), one entry per emulator, the same six in the
same order on every platform, each with that platform's build or none. This table is
Windows x64's; [Linux and macOS](#linux-and-macos) has the others.

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

## Linux and macOS

`Downloads::Platform` is Windows x64, Linux x86_64, Linux arm64, macOS arm64, macOS x86_64
or another system (nothing offered). `hostPlatform()` is decided when RetroFE is compiled;
`emulators(platform)` gives any platform's table, so Linux CI tests every one, and tests
choose the table in use with `usePlatform()`. What each emulator publishes was checked by
hand on 2026-11-27; **every row below is unverified** until a real download is installed
and launched on that system, and so are the launcher arguments (each the Windows one, as
it's the same program's command line) and the portable rules.

| Platform | Emulator | Download | Form | Program | Portable | Verified |
|---|---|---|---|---|---|---|
| Linux x86_64 | RetroArch | `https://buildbot.libretro.com/stable/<version>/linux/x86_64/RetroArch.7z` (no checksum), top folder `RetroArch-Linux-x86_64/` removed | 7z | `RetroArch-Linux-x86_64.AppImage` | the archive's `RetroArch-Linux-x86_64.AppImage.home/` (kept) | no |
| Linux x86_64 | DuckStation | `DuckStation-x64.AppImage` on the rolling release `latest` | AppImage | `DuckStation.AppImage` | `portable.txt` | no |
| Linux x86_64 | PCSX2 | `pcsx2-v<version>-linux-appimage-x64-Qt.AppImage` (`/releases/latest`) | AppImage | `PCSX2.AppImage` | `portable.txt` | no |
| Linux x86_64 | PPSSPP | `PPSSPP-v<version>-anylinux-x86_64.AppImage` | AppImage | `PPSSPP.AppImage` | none (its home folder) | no |
| Linux x86_64 | Flycast | `flycast-x86_64-<version>.AppImage` | AppImage | `Flycast.AppImage` | none (its home folder) | no |
| Linux x86_64 | MAME | none (Windows builds only) | | | | |
| Linux arm64 | DuckStation | `DuckStation-arm64.AppImage` on the rolling release `latest` | AppImage | `DuckStation.AppImage` | `portable.txt` | no |
| Linux arm64 | PPSSPP | `PPSSPP-v<version>-anylinux-aarch64.AppImage` | AppImage | `PPSSPP.AppImage` | none (its home folder) | no |
| Linux arm64 | RetroArch, PCSX2, Flycast, MAME | none (no stable RetroArch, so no cores) | | | | |
| macOS | RetroArch | `https://buildbot.libretro.com/stable/<version>/apple/osx/universal/RetroArch_Metal.dmg` (no checksum) | DMG, `RetroArch.app` copied out | `RetroArch.app/Contents/MacOS/RetroArch` | none | no |
| macOS | DuckStation | `duckstation-mac-release.zip` on the tagged `v0.1-NNNN` releases | zip | `DuckStation.app/Contents/MacOS/DuckStation` | none | no |
| macOS | PPSSPP | `PPSSPPSDL-macOS-v<version>.zip` (links inside `SDL2.framework`) | zip | `PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL` | none | no |
| macOS | Flycast | `flycast-macOS-<version>.zip` | zip | `Flycast.app/Contents/MacOS/Flycast` | none | no |
| macOS | PCSX2 | not offered yet (a `.tar.xz`) | | | | |
| macOS | MAME | none | | | | |

AppImages are installed under a fixed name, so the launcher doesn't change with the
version. macOS arm64 and x86_64 have the same table; their cores differ.

- **Cores:** `https://buildbot.libretro.com/nightly/linux/x86_64/latest/<core>_libretro.so.zip`
  and `nightly/apple/osx/<arm64|x86_64>/latest/<core>_libretro.dylib.zip`, each with the
  `.index-extended` beside it, cached as `cores-<platform>.index` (Windows keeps
  `cores.index`). A core may write only its own library (`.so`, `.dylib`). Core names come
  from RetroArch's info files where the download puts them (the AppImage's
  `.home/.config/retroarch/info/`, `RetroArch.app/Contents/Resources/info/`; unverified),
  otherwise the file stem. Linux arm64 has none.
- **Rolling releases:** DuckStation's Linux builds are only on the release tagged
  `latest` (not a pre-release; `/releases/latest` returns it). Its version is the
  release's `published_at` date as `YYYY.MM.DD`, so a newer build compares newer. Every
  other entry keeps its tag rule. The asset still needs GitHub's SHA-256. The build is
  replaced in place, so a list read earlier may name the last build's SHA-256: when the
  download doesn't match, the list is read again at once (whatever its age) and, if it now
  names another download, that one is fetched, once; otherwise the install fails as any
  mismatch does.
- **Launchers** go in `launchers.linux/` or `launchers.apple/` (`Downloads::launcherOs`,
  the names `Utils::getOSType()` gives), with `%RETROFE_PATH%/...` paths; a core's is
  `-L "%RETROFE_PATH%/emulators/RetroArch/cores/<core>_libretro.so" "%ITEM_FILEPATH%"`.
  Add a system knows the programs above (Manager/Emulators/KnownEmulators).
- **Release lists** are cached per platform (`cache/<entry>-linux-x86_64/`...; Windows keeps
  `cache/<entry>/`), so a drive used on several systems doesn't mix them. What's installed
  (`installed/<id>.json`, `emulators/<folder>/`) isn't per platform: an emulator installed
  on one system is installed for the other too.
- **HTTP:** libcurl (`Downloads/Http.cpp`; `find_package(CURL)` off Windows,
  `libcurl4-openssl-dev` on Linux CI, the SDK's on macOS), behind the same `Http` as
  WinHTTP: extra request headers, every response's status and headers (lower-case names,
  the last response's after redirects), the ETag or Last-Modified kept for `If-Range`,
  `Range` to continue, progress and Stop (also while waiting), 15 s to connect and give
  up after 30 s without data (WinHTTP's timeouts), downloads only over https://, and
  redirects followed only to https:// (libcurl's protocol limits, and each `Location`
  checked). The status is libcurl's own (`CURLINFO_RESPONSE_CODE`); header lines are read
  per answer (`HttpText::HeaderReader`: a status line starts an answer only after a blank
  line, so one shaped like it among the headers is ignored). SIGPIPE is ignored for the
  process once, when the first curl Http is made (`CURLOPT_NOSIGNAL` is set). Its text
  handling (`Downloads::HttpText`: status lines, headers, Range, validators, redirects,
  what an answer means) is built and tested everywhere.

### AppImages, disk images, permissions and links

- **AppImage:** the download is the program, not unpacked. It must start with an ELF
  header (`\x7fELF`), or it's refused; it's copied into staging as the entry's program
  with mode 0755, then committed like any file.
- **DMG (macOS only):** the image must end with a UDIF trailer. It's attached with
  `hdiutil attach -readonly -nobrowse -noautoopen -mountpoint <staging>/<id>/mount`
  (both full paths; `/usr/bin/hdiutil` through `posix_spawn`, no shell, no input, at most
  five minutes), the entry's `.app` folder is copied out with the same checks as archive
  entries (anything that isn't a file, folder or link is refused), and the image is always
  detached (`hdiutil detach`, then `-force`, logged if both fail), even when the copy fails
  or the attach failed or timed out. Before any staging folder is deleted (after an
  install, a failure or in recovery) an image still attached there is detached first (the
  mount point on another device than its folder); if it stays attached the folder is left.
  Nothing in the image is opened or run. The opener is a hook
  (`Installer::setImageOpener`: attach, detach, mounted): elsewhere there's none and a DMG
  can't be installed; tests give a fake.
- **Permissions:** unpacking zip (made on Unix or macOS: the external attributes' high
  16 bits) and 7z (the Unix extension, 0x8000: the attributes' high 16 bits) keeps the
  stored executable bits, with read and write for the owner, never write for others, and
  never setuid, setgid or sticky. The entry's program is made executable even when the
  archive stored nothing. Whatever the process's umask, a file without a stored mode is
  0644 (portable files too), the program and an AppImage 0755, and every folder the
  installer makes (in staging and in `emulators/`) 0755. The mode is in the plan and the
  manifest (`"mode"`, at most 0777), reapplied after each move. Windows ignores modes, as
  before.
- **Links (Linux and macOS):** an archive's (or image's) symbolic link is recreated only
  if its target is relative (no backslash, drive or control character) and, resolved from
  its own folder, names something strictly inside the emulator's folder; no entry may go
  inside a link, nor a link over a folder of entries. Once staged, and again once moved
  into place, every link is followed as the system follows it (through other links, a
  link's `..` taken from where it leads, at most 40) and must still stay inside; anything
  else refuses the whole install as unsafe. Each link is also followed as soon as it's
  made, so none is left in staging leading out. Names (of files and links) may not have
  empty or `.` parts (`./a`, `a//b`, `a/./b`), so one path has one spelling. Nothing is
  ever written through a link, whatever its name's spelling (APFS ignores case, and not
  only ASCII's): every folder a file goes in is made one at a time, from the emulator's
  folder down, each part that's there checked with `lstat` (a link or a file refuses),
  in staging and again at commit (where only the emulator's folder itself may be a link:
  the user's, to another drive). Links are recorded in the manifest (`"link"`) and checked
  again at commit, recovery and removal: an update or removal compares and removes the
  link itself, never what it leads to, and keeps a link changed since.
- **Windows** treats links as it always did (it follows them, and backs up and restores
  by content). What changed in round 39: an archive entry that is a link (made on Unix)
  is refused, and the install with it; before, it became a small file holding the link's
  target.

### Limits (accepted)

- An update that turns a link into a folder, or a folder into a link, can't be installed
  (it fails safe, nothing changed): remove the emulator and install it again.
- A core installed by RetroFE on another system (a shared drive: `.dll` beside `.so`)
  isn't this system's core, so removing cores here leaves it behind.
- Stop isn't seen while `hdiutil attach` runs; it waits for it (at most its five minutes).
- `curl_global_init` is only thread-safe from libcurl 7.84 (Ubuntu 24.04 has 8.5); RetroFE
  calls it once, from the first download screen.

## How a release is found

Each entry's releases are read from GitHub's API (`/repos/<repo>/releases`), cached in
`manage/downloads/cache/<entry>/` with their `ETag`: an unauthenticated `304` counts against
GitHub's 60 requests an hour. Six requests per check allow about ten checks an hour. Automatic checks reuse lists for an hour; Check again asks GitHub after a one-minute floor. The screen shows the oldest successful check time, and when the limit
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

- **Archives:** `.zip` (minizip), `.7z`, and 7-Zip self-extracting `.exe` files (and, off
  Windows, AppImages and macOS disk images: [above](#appimages-disk-images-permissions-and-links)), read with
  the LZMA SDK's C decoder (public domain, in `RetroFE/ThirdParty/lzma-sdk`). A
  self-extracting archive is opened by finding the 7-Zip signature after the program stub;
  **nothing downloaded is ever run.** The usual path checks apply to every name inside.
  RetroArch's `.7z` uses LZMA2, LZMA and BCJ2; PCSX2's and MAME's use LZMA2. All are in
  the SDK's decoder.
- **Programs are allowed** only for entries of the built-in list with a build for this
  platform, and only under `emulators/<folder>/`. A core may write only its own
  `RetroArch/cores/<core>_libretro.dll` (`.so`, `.dylib`).
- **The folder must be the installer's.** If `emulators/<folder>/` exists and wasn't
  installed by RetroFE, the install stops: "emulators/MAME is already there. Rename it,
  or add a launcher for it instead."
- **Portable files** the entry lists are created if missing (empty). They're recorded in
  the manifest as created, so removing the emulator removes them.
- **The launcher** `launchers.windows/<name>.conf` (`launchers.linux/`, `launchers.apple/`) is written with `%RETROFE_PATH%` paths,
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
  "Install RetroArch first". Linux and macOS have their own cores ([above](#linux-and-macos));
  where RetroArch has no build (Linux arm64) the cores link and screens don't show.
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
say downloads are supported (`DownloadProvider::Hooks`), and choose the platform's table,
so they run on any system.

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
  Windows releases, and every Linux and macOS row above with real ones there. The tests
  (DownloadsTests, DownloadScreensTests for Windows x64's table; DownloadsPlatformTests
  for the others, AppImages, disk images through a fake opener, modes and links) use
  release JSON and local archives only; the 7z ones come from a small writer of stored
  files (Tests/SevenZipWriter.h).
