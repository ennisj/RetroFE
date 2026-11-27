# Continuous integration

The GitHub Actions workflows are in `.github/workflows`.

## Builds and unit tests: `build.yml`

`build.yml` builds RetroFE and the `retrofe_tests` unit tests (`RetroFE/Tests`), then runs the tests. It runs:

- on pushes to `master`, `feature/**` and `ci/**` branches, and on pull requests to `master` and `feature/**`, when something under `RetroFE/CMake`, `RetroFE/Source`, `RetroFE/ThirdParty`, `RetroFE/ThirdPartyMac`, `RetroFE/Tests`, Python files under `Scripts/test-build`, `Scripts/test-build/fixtures`, `Scripts/test-build/screens`, `.github/workflows` or `.github/scripts` changes
- by hand: `gh workflow run build.yml --ref <branch>`, or "Run workflow" on the Actions tab

A pull request from a `feature/**` branch gets two runs per push: one for the push and one for the pull request. Both run the same jobs. A newer push to a branch cancels that branch's run still in progress, except on `master`, where every run finishes. The workflows only read the repository (`permissions: contents: read`), except to publish: `release-windows.yml` for releases and `build.yml`'s Windows job for the test build and ci-screens screenshots below.

It has four jobs:

| Job | Runner | Libraries |
| --- | --- | --- |
| Linux x86_64 | `ubuntu-24.04` | apt (`libsdl2-dev`, `libgstreamer1.0-dev`, ...) |
| Linux arm64 | `ubuntu-24.04-arm` | the same |
| Windows x64 | `windows-2022` | GStreamer's development MSI, the rest from `RetroFE/ThirdParty` |
| macOS arm64 | `macos-15` | Homebrew (`sdl2`, `gstreamer`, `minizip`, ...); `sdl2` is now `sdl2-compat`, SDL2 on top of SDL3 |

**Every platform runs on every push** (since round 36; [DECISIONS.md](DECISIONS.md) W4). The Windows job also runs the screenshot scripts and publishes the test build; Linux and macOS build, test and start `retrofe --version`.

Each configures with `-DRETROFE_BUILD_TESTS=ON`, builds everything, and runs the tests with `ctest --test-dir RetroFE/Build --output-on-failure` (`-C Release` on Windows). When Python is found, ctest also runs the test-build scripts' Python tests. ctest runs `retrofe_tests`, which exits non-zero if any check fails and prints each failure, so a failing check fails the job. On Windows, ctest runs with `-V` and the job repeats compiler and linker errors, failing checks and the closing "N/M checks passed" line as annotations: unlike the log, annotations can be read without signing in (`GET /repos/ennisj/RetroFE/check-runs/<job id>/annotations`), so an agent without a GitHub login can still see why a run failed. The Linux and macOS jobs do the same for compiler and linker errors (with the missing symbols) and failed checks, and on macOS a failed build also reports Homebrew's SDL2_mixer setup as warnings. The Linux and macOS jobs also run `retrofe --version`, which exits before opening a window: a quick check that the executable starts and its libraries load.

None of the tests need a display, a GPU or SDL: `retrofe_tests` is built only from the code that doesn't use SDL (see the `RETROFE_BUILD_TESTS` block in `RetroFE/Source/CMakeLists.txt`). A test that needed a window couldn't run on these runners.

To run the tests locally:

```sh
cmake -S RetroFE/Source -B RetroFE/Build -DRETROFE_BUILD_TESTS=ON
cmake --build RetroFE/Build --config Release
ctest --test-dir RetroFE/Build -C Release --output-on-failure
```

### GStreamer on Windows

The Windows workflows (`build.yml`, `build-windows.yml`, `release-windows.yml`) get GStreamer through `.github/scripts/install-gstreamer-windows.ps1`. The script unpacks the MSVC x86_64 development MSI from gstreamer.freedesktop.org with an administrative install and sets `GSTREAMER_ROOT`. GStreamer removes older releases from that site; Chocolatey's `gstreamer-devel` 1.24.11 broke that way. If the download starts failing with 404, raise the script's `$Version` and `$Sha256`. Take the hash from the `.sha256sum` file GStreamer publishes beside the MSI, and check it against the file you download. The script checks the MSI against `$Sha256` both after downloading it and when it comes from the Actions cache. A copy that doesn't match is deleted and the job fails. Only the headers and import libraries come from there. The DLLs RetroFE ships with are in the `Package/Environment/Windows/retrofe` submodule.

### Screenshots of the real program (Windows)

After the unit tests, the Windows job runs `retrofe.exe` in a 960x540 window on the runner and drives it with the scripts in `Scripts/test-build/screens/scripts` (key presses posted to its window, a screenshot after each step; see [Scripts/test-build/screens/README.md](../Scripts/test-build/screens/README.md)). A script named `bare-*` runs in a bare install (as `RetroFE-Bare` on the PC); one named `starter-*` runs in the install the first-setup test leaves behind (ctest runs with `RETROFE_KEEP_FIRST_SETUP` set), with stand-in games and artwork and the test's fake RetroArch. Each push publishes the screenshots, `results.json`, RetroFE's `log.txt` and console output as `screens-<first 8 characters of the commit>.zip` on the `ci-screens` pre-release (the 30 newest are kept), downloadable without signing in:

```
https://github.com/ennisj/RetroFE/releases/download/ci-screens/screens-<commit>.zip
```

A failing script fails the build (an error annotation names it), except a script with a first line starting with `# soft-fail`, which is still settling: ordinary script-step failures are warning annotations; crashes and install or startup failures still fail the build (DECISIONS M34, M38). The screenshots are published either way. When RetroFE crashes, the step runs it again under `cdb` and adds the stack and module list to the zip as `cdb.txt`. The runner has no GPU or sound device: what it can't show is how things look on real hardware, at full size, with videos, sound, controllers or fullscreen.

## Packages

These run only by hand (`workflow_dispatch`) and upload the package as an artifact:

- `build-windows.yml`: `retrofe/` with its DLLs, and `RetroFE.lnk`
- `build-linux.yml`: AppImages for x86_64 and arm64
- `build-macos.yml`: `RetroFE.dmg`, holding a universal (arm64 and x86_64) `RetroFE.app` for macOS 11 and later

`release-windows.yml` runs when a `v*` tag is pushed, such as `v2504.3`, and attaches `RetroFE-2504.3-windows-x64.zip` (`retrofe\` plus `RetroFE.lnk` and the layouts in `RELEASE_LAYOUTS`) and its `.sha256` to that tag's release. Tags with a `-` (e.g. `v2504.3-beta.1`) become pre-releases. A release build reports its tag's version; other builds report the version in `RetroFE/CMake/Versioning.cmake` (see the workflow's comments).

### The test build of `feature/management-ui`

When the Windows job passes on a push to `feature/management-ui`, its last step replaces the `management-ui-latest` pre-release with `RetroFE-management-ui-windows-x64.zip` and its `.sha256`. The zip holds `retrofe\retrofe.exe`, `layouts\Aeon Nox` and `BUILD.txt` (branch, commit, date, subject and the run's link), laid out like an install. It has no DLLs, so it goes over an install that already has them (`E:\Storage\OneSauce`). Anyone can download it without signing in:

```
https://github.com/ennisj/RetroFE/releases/download/management-ui-latest/RetroFE-management-ui-windows-x64.zip
```

The release is deleted and created again each time, so its tag always points at the commit that was built. `Scripts/test-build/deploy_test_build.py` downloads it, checks it against its `.sha256` and copies it onto the PC's `RetroFE-*` test installs (see [Scripts/test-build/README.md](../Scripts/test-build/README.md)).

## macOS builds

macOS builds with CMake in one of two ways (`RETROFE_MACOS_BUNDLE`):

- Against Homebrew's libraries (`brew install pkg-config sdl2 sdl2_image sdl2_mixer sdl2_ttf gstreamer minizip webp libusb`), giving a plain `retrofe` executable. This is what `build.yml` does, and is the default.
- `RetroFE.app` against the frameworks in `RetroFE/ThirdPartyMac`: SDL2, SDL2_image (and its `webp.framework`), SDL2_mixer and SDL2_ttf from their release disk images, `GStreamer.framework` from GStreamer's installer, and the universal `libminizip.dylib` and `libusb-1.0.0.dylib` already there. `build-macos.yml` puts those in place. Once `SDL2.framework` is in `ThirdPartyMac` this is the default. CMake copies the frameworks into `RetroFE.app/Contents/Frameworks` and signs the bundle ad hoc (`RetroFE/CMake/EmbedMacFrameworks.sh`). `build-macos.yml` then slims `GStreamer.framework` (`Scripts/macOS`), signs the bundle again and checks the signature (`codesign --verify --deep --strict`). It then checks that `RetroFE.app` is self-contained: every run path must be under `@executable_path/../Frameworks`, and starting it with `DYLD_PRINT_LIBRARIES=1` (both halves, x86_64 under Rosetta) must load nothing from outside the bundle, `/usr/lib` or `/System`. Last, it makes the disk image.

The Xcode project in `RetroFE/xcode` predates the management UI and doesn't build the current source. CI doesn't use it.

The `setup-*` screens install stages the fake RetroArch and downloads before setup, then drives the first setup in the window. Its browser press counts need tuning on Windows CI. The screens run timeout is 240 seconds to allow setup and copying.
