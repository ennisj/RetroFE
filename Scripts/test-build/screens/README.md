# Screenshot test driver

This stdlib-only tool prepares small RetroFE installs and drives the real Windows executable with posted keyboard messages. For CI picture publication, see docs/CI.md.

```
python -I Scripts/test-build/screens/screens.py install --kind bare --exe path/retrofe.exe --dlls path/DLLs --common Package/Environment/Common out/bare
python -I Scripts/test-build/screens/screens.py install --kind starter --exe path/retrofe.exe --dlls path/DLLs --common Package/Environment/Common --setup path/kept-setup out/starter
python -I Scripts/test-build/screens/screens.py run --install out/bare --script Scripts/test-build/screens/scripts/bare-first-start.txt --out out/pictures --width 960 --height 540 --timeout 120
```

Run `RETROFE_KEEP_FIRST_SETUP=/path/to/empty-destination retrofe_tests` to retain the completed FirstSetupTests root, then pass that path as `--setup`. The test uses stand-in ROMs and a fake emulator. An existing nonempty install destination is refused. Run writes numbered PNGs, results.json, and a copy of log.txt. A one-colour capture is marked `blank` in results.json.

Scripts use one command per line; blank lines and lines starting with `#` are ignored:

- `wait SECONDS`, `wait-drawn [TIMEOUT]` (until the window shows anything; default 60 seconds), `wait-log TEXT [TIMEOUT]` (default 30 seconds; a trailing number is interpreted as the timeout; RetroFE logs little at its default level)
- `key NAME [xN] [hold SECONDS]` (default hold 0.1 seconds), `type TEXT`
- `shot NAME`, `expect-file PATH [TEXT]`, `expect-log TEXT`, `quit` (nothing may follow `quit`)
- `expect-differs SHOTNAME LEFT TOP RIGHT BOTTOM` captures again and compares the specified region with an earlier named shot. Coordinates are fractions from 0 to 1. More than 0.1% of region pixels must change by over 8 in at least one RGB channel, allowing minor capture noise; choose a region that excludes unrelated animation such as the clock.

File paths are relative to the install and cannot contain spaces. Key names include arrows, Return, Escape, Space, Tab, Backspace, Delete, Home, End, PageUp, PageDown, F1–F12, letters and digits. `type` posts character messages. The whole script is validated before launch. A failure exits 1 and is recorded in results.json.

Pictures need human review for appearance. This cannot validate real GPUs, real emulators, or ImGui mouse input: posted mouse clicks do not reach ImGui. Windows capture and input need Windows CI; portable parsing, image encoding and install construction are tested on Linux.
