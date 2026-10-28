# Continuous integration

The GitHub Actions workflows are in `.github/workflows`.

## Builds and unit tests: `build.yml`

`build.yml` builds RetroFE and the `retrofe_tests` unit tests (`RetroFE/Tests`), then runs the tests. It runs:

- on pushes to `master`, `feature/**` and `ci/**` branches, and on pull requests to `master` and `feature/**`, when something under `RetroFE/CMake`, `RetroFE/Source`, `RetroFE/ThirdParty`, `RetroFE/ThirdPartyMac`, `RetroFE/Tests`, `.github/workflows` or `.github/scripts` changes
- by hand: `gh workflow run build.yml --ref <branch>`, or "Run workflow" on the Actions tab

A pull request from a `feature/**` branch gets two runs per push: one for the push and one for the pull request. Both run the same jobs. A newer push to a branch cancels that branch's run still in progress, except on `master`, where every run finishes. The workflows only read the repository (`permissions: contents: read`); `release-windows.yml` alone can write, to publish releases.

It has four jobs:

| Job | Runner | Libraries |
| --- | --- | --- |
| Linux x86_64 | `ubuntu-24.04` | apt (`libsdl2-dev`, `libgstreamer1.0-dev`, ...) |
| Linux arm64 | `ubuntu-24.04-arm` | the same |
| Windows x64 | `windows-2022` | GStreamer's development MSI, the rest from `RetroFE/ThirdParty` |
| macOS arm64 | `macos-14` | Homebrew (`sdl2`, `gstreamer`, `minizip`, ...) |

**For now only Windows x64 runs on every push** (development is Windows-first, [DECISIONS.md](DECISIONS.md) W4). The Linux and macOS jobs run for `master`, pull requests to `master`, and runs started by hand, so a branch can still be checked on every platform with `gh workflow run build.yml --ref <branch>`.

Each configures with `-DRETROFE_BUILD_TESTS=ON`, builds everything, and runs the tests with `ctest --test-dir RetroFE/Build --output-on-failure` (`-C Release` on Windows). ctest runs `retrofe_tests`, which exits non-zero if any check fails and prints each failure, so a failing check fails the job. On Windows, ctest runs with `-V` and the job repeats compiler and linker errors, failing checks and the closing "N/M checks passed" line as annotations: unlike the log, annotations can be read without signing in (`GET /repos/ennisj/RetroFE/check-runs/<job id>/annotations`), so an agent without a GitHub login can still see why a run failed. The Linux and macOS jobs also run `retrofe --version`, which exits before opening a window: a quick check that the executable starts and its libraries load.

On Windows the tests run with `RetroFE/Tests/test_server.py` listening on port 8765 and `RETROFE_TEST_SERVER` pointing to it, so the Catalog's and self-update's real HTTP tests run too. Linux and macOS have no HTTP client yet (`Catalog/Http.cpp`), so those tests skip themselves there.

None of the tests need a display, a GPU or SDL: `retrofe_tests` is built only from the code that doesn't use SDL (see the `RETROFE_BUILD_TESTS` block in `RetroFE/Source/CMakeLists.txt`). A test that needed a window couldn't run on these runners.

To run the tests locally:

```sh
cmake -S RetroFE/Source -B RetroFE/Build -DRETROFE_BUILD_TESTS=ON
cmake --build RetroFE/Build --config Release
ctest --test-dir RetroFE/Build -C Release --output-on-failure
```

### GStreamer on Windows

The Windows workflows (`build.yml`, `build-windows.yml`, `release-windows.yml`) get GStreamer through `.github/scripts/install-gstreamer-windows.ps1`. The script unpacks the MSVC x86_64 development MSI from gstreamer.freedesktop.org with an administrative install and sets `GSTREAMER_ROOT`. GStreamer removes older releases from that site; Chocolatey's `gstreamer-devel` 1.24.11 broke that way. If the download starts failing with 404, raise the script's `$Version` and `$Sha256`. Take the hash from the `.sha256sum` file GStreamer publishes beside the MSI, and check it against the file you download. The script checks the MSI against `$Sha256` both after downloading it and when it comes from the Actions cache. A copy that doesn't match is deleted and the job fails. Only the headers and import libraries come from there. The DLLs RetroFE ships with are in the `Package/Environment/Windows/retrofe` submodule.

## Packages

These run only by hand (`workflow_dispatch`) and upload the package as an artifact:

- `build-windows.yml`: `retrofe/` with its DLLs, and `RetroFE.lnk`
- `build-linux.yml`: AppImages for x86_64 and arm64
- `build-macos.yml`: `RetroFE.dmg`, holding a universal (arm64 and x86_64) `RetroFE.app` for macOS 11 and later

`release-windows.yml` runs when a `v*` tag is pushed and attaches the Windows archive to that tag's release (see the workflow's comments).

## macOS builds

macOS builds with CMake in one of two ways (`RETROFE_MACOS_BUNDLE`):

- Against Homebrew's libraries (`brew install pkg-config sdl2 sdl2_image sdl2_mixer sdl2_ttf gstreamer minizip webp libusb`), giving a plain `retrofe` executable. This is what `build.yml` does, and is the default.
- `RetroFE.app` against the frameworks in `RetroFE/ThirdPartyMac`: SDL2, SDL2_image (and its `webp.framework`), SDL2_mixer and SDL2_ttf from their release disk images, `GStreamer.framework` from GStreamer's installer, and the universal `libminizip.dylib` and `libusb-1.0.0.dylib` already there. `build-macos.yml` puts those in place. Once `SDL2.framework` is in `ThirdPartyMac` this is the default. CMake copies the frameworks into `RetroFE.app/Contents/Frameworks` and signs the bundle ad hoc (`RetroFE/CMake/EmbedMacFrameworks.sh`). `build-macos.yml` then slims `GStreamer.framework` (`Scripts/macOS`), signs the bundle again and checks the signature (`codesign --verify --deep --strict`). It then checks that `RetroFE.app` is self-contained: every run path must be under `@executable_path/../Frameworks`, and starting it with `DYLD_PRINT_LIBRARIES=1` (both halves, x86_64 under Rosetta) must load nothing from outside the bundle, `/usr/lib` or `/System`. Last, it makes the disk image.

The Xcode project in `RetroFE/xcode` predates the management UI and doesn't build the current source. CI doesn't use it.
