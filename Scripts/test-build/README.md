# Test builds on the Windows PC

Each passing Windows CI run on `feature/management-ui` publishes the `management-ui-latest` pre-release ([docs/CI.md](../../docs/CI.md)). Each install below is its own RetroFE root, with its own settings and data. `RetroFE-Bare` is empty for first-run testing; `RetroFE-Starter` is a small first setup replayed by hand. A larger install can be added later.

```
\\JOHN-DESKTOP\retrofe\
├── RetroFE-Bare\retrofe\retrofe.exe
├── RetroFE-Starter\retrofe\retrofe.exe
├── fixtures\starter\<System>\roms\
├── builds\<date>-<commit>\
└── backups\
```

`deploy_test_build.py` downloads the latest build, verifies its SHA-256 and keeps it under `builds`. It updates every top-level `RetroFE-*` install with `retrofe\retrofe.exe`, sorted by name; if none exists it creates `RetroFE-Bare`. Use `--install NAME` (repeatable) to choose installs. A new install gets RetroFE's DLLs and fonts; later deployments copy over the executable and layout, leaving the rest alone. If RetroFE is running, close it and retry. `BUILD.txt` identifies the installed build.

```sh
python3 Scripts/test-build/deploy_test_build.py --path /mnt/remotes/JOHN-DESKTOP_retrofe
python3 Scripts/test-build/make_test_install.py --fixtures starter --path /mnt/remotes/JOHN-DESKTOP_retrofe
python3 Scripts/test-build/make_test_install.py --fixtures starter --reset --path /mnt/remotes/JOHN-DESKTOP_retrofe
```

Both scripts can use SMB instead of `--path`, with `--host`, `--share` and `--credentials` (the default credentials file contains the user name and password on separate lines). SMB needs the optional `smbprotocol` package. The fixture script prints the folders to choose in Manage. `--install NAME` selects another install; without `--reset` an existing install is left alone. Reset moves the whole install into `backups` before creating a fresh install, including any RetroArch installed in its `emulators` folder in checklist step 1; move it back or install it again. If deployment fails, the original is restored and the partial install is kept in `backups` with a `-failed` suffix. `--source DIR` overrides the default OneSauce collections folder. The OneSauce share is read-only: the script only reads it. `--path` must already be a folder and cannot overlap `--source` (including through symlinks). `--pc` selects the PC name in printed From paths (default `JOHN-DESKTOP`). The default install name cannot be `RetroFE-Bare`; select it explicitly with `--install RetroFE-Bare` if intended.

Fixture manifests in `fixtures/<name>.txt` contain one path relative to the OneSauce collections folder per line. Blank lines and `#` comments are ignored. A trailing `/` copies a whole folder recursively; `source => target` stages it at another relative path. File and folder targets must match their source type; overlapping folder targets and Windows-invalid paths are rejected. All sources must exist before staging begins. Existing staged files of the same size are skipped; different sizes are replaced. The repository is public: ROMs and artwork never go in it, only their paths.

## Starter checklist

Run `make_test_install.py --fixtures starter` first. On the PC, start `RetroFE-Starter\retrofe\retrofe.exe`.

1. Install RetroArch for Windows x64 under `RetroFE-Starter\emulators\RetroArch`, with the `fceumm`, `genesis_plus_gx` and `mame2003_plus` cores (OneSauce's MAME files are sets for `mame2003_plus`, not current MAME). In RetroArch, run **Online Updater > Update Core Info Files**.
2. Manage > Launchers: add a launcher for each core (Program `retroarch.exe`, then its Core file).
3. Manage > Systems > **Add a system**, three times, choosing each launcher. The name and extensions come from the core's `.info` file. Type the names `Nintendo Entertainment System`, `Sega Genesis` and `MAME`, so they match the staged folders. For NES, add `dat` to the extensions: `fceumm` doesn't list it, and Little Lancelot is a `.dat`.
4. For each system choose **Add games**. In **From**, pick its staged `roms` folder, leave **Copy artwork too** on, and copy. NES with **Include subfolders** on finds 12 games (Contra is in `More games`), Genesis 10 and MAME 10. A second Copy counts them all as already there.
5. Close Manage. Browse lists every game with its exact name (`Addams Family, The (USA)`, `Bill & Ted's...`, `Awesome Possum... ...`). Start one game from each system.
6. Check Browse and a game's Media screen in Manage. Excitebike has no art, and Metroid's art has no game and is not copied.
7. Expected, not a bug, until there is a game database:
   - MAME shows file names (`1942`, `dkong`), because a release ships no `meta\` database.
   - Genesis front art and logos are named after titles (`Sonic The Hedgehog.png`), so they don't match the files. Only the stories show.
   - pacman's front art is under its parent `puckman`, which RetroFE can't know without metadata.

`RetroFE/Tests/FirstSetupTests.cpp` (suite "first setup", run by ctest) replays steps 2-6 through Manage's model on an empty root, with stand-ins for the Starter files and `retrofe_fake_emulator` as RetroArch: the three launchers, the names and extensions suggested from the cores' `.info` files, the systems in `settings.conf` and the top menu, Add games' counts and a second Copy, every game listed as Browse builds it and found in Games, a game per system (and Contra, and Little Lancelot's `.dat`) started with the command RetroFE builds (`-L`, the core and the game each one argument, in RetroArch's folder), and the Media screens after Add games copies the matching artwork. The PC is still needed for step 1 and real RetroArch with its cores (that the games actually play), how the screens look, controllers, and fullscreen.

Windows CI also runs the real program in both kinds of install and publishes screenshots of each step: [screens/README.md](screens/README.md) and [docs/CI.md](../../docs/CI.md).
