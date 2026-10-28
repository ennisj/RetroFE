# Things to verify

Behaviour that was worked out from documentation or source code, or covered
only by unit tests, and still needs a check on a real setup. Update the
**Status** column as you test: **To check**, **OK**, or **Wrong** (with a
note on what happened, so it can be fixed).

Platform: Windows unless the item says otherwise (development is
Windows-first for now, [DECISIONS.md](DECISIONS.md) W4).

## RetroArch

Needs a cabinet or PC with a real RetroArch install and a system whose
launcher runs it.

| # | What to check | How | Status | Notes |
|---|---------------|-----|--------|-------|
| R1 | The "Default (…)" values on a game's RetroArch screen match what RetroArch actually uses | Manage → Games → a RetroArch game → RetroArch. Compare each "Default (…)" with RetroArch's own menu for that game (Quick Menu → Options / Settings). | To check | From reading RetroArch's source ([DECISIONS.md](DECISIONS.md) M17) |
| R2 | A setting changed in Manage takes effect in the game | Change e.g. aspect ratio or smoothing for one game, Save, start the game. Then "Use the system's settings" and check it's back. | To check | Writes `<config>/<Core>/<rom>.cfg` |
| R3 | Core options and controls changed in Manage take effect | As R2, for a core option and one remapped button | To check | `.opt` / `.rmp` files |
| R4 | The core's settings folder name is right for the cores you use | A system's RetroArch screen: a "not confirmed" note means Manage guessed the folder from the core's `.info` file | To check | The hand-checked table covers common cores |
| R5 | Saves are found where RetroArch keeps them | Manage → Games → a game you've saved in → Saves. Try with RetroArch's default save settings, and if you use them, "Sort saves into folders by core/content" and "Write saves to content directory". | To check | From reading RetroArch's source; the reviewer read one case differently (M18) |
| R6 | Back up / Restore of saves works with RetroArch | Back up, play and save, Restore, start the game: the older save should load | To check | |

## Controls and controllers

| # | What to check | How | Status | Notes |
|---|---------------|-----|--------|-------|
| C1 | On a bare install, Select/Back work on an arcade USB encoder (a joystick with no SDL controller mapping, e.g. Zero Delay) | Start RetroFE with no `controls.conf`; press the encoder's buttons 1 and 2 | To check | Unit tests only (M19) |
| C2 | On a bare install, Select/Back work on a gamepad (Xbox or PlayStation style) and one button never does both | As C1 with a pad: A/Cross selects, B/Circle goes back | To check | |
| C3 | A controller plugged in while RetroFE runs works, and unplugging/replugging doesn't break it | Plug in after start; unplug and replug; also with Manage open | To check | |
| C4 | Settings → Controls captures a real controller button, stick direction and trigger | Add an input by pressing each | To check | Checked with posted keyboard keys only |
| C5 | OneSauce's own `controls.conf` works with your actual controls | Run on a OneSauce install with no `controls1.conf` remap | To check | Starts without warnings in testing |
| C6 | letterUp / letterDown on a pad (shoulder or trigger) jump through a Manage list by letter, and move the cursor while the on-screen keyboard is up | Manage → Games: jump through the results; then edit Search and press them with the keyboard shown | To check | Checked with the letterDown key (N) on lists only: a key press puts the on-screen keyboard away, so its cursor move was checked in unit tests |
| C7 | The editors' hint strip says the right thing for each mode with a real controller | Themes → a theme → Edit this theme; Pick, Move, Resize, hold Select, a panel, a dialog | To check | Checked with posted keys |

## Launching and closing

| # | What to check | How | Status | Notes |
|---|---------------|-----|--------|-------|
| L1 | Closing the window works after a game when `unloadSDL = true` | Set `unloadSDL = true`, start and quit a game, then click the window's close button | To check | Fixed in code only; testing it needed a real game launch |
| L2 | A window close clicked while a game runs doesn't quit RetroFE afterwards | Start a game, close the game's window, return | To check | Fixed in code only |
| L3 | Play counts and time played update after playing (with `lastPlayedSkipCollection` set) | Play a game for a minute, look at its Manage screen | To check | |
| L4 | After Manage's "Start this game", Manage comes back with focus on Start this game | Manage → Games → a game → Start this game; quit the game | To check | Not launched in the GUI check (a launch raises RetroFE's window over the desktop); the focus request is the same one tested for other actions |

## OnesaUCE themes

| # | What to check | How | Status | Notes |
|---|---------------|-----|--------|-------|
| T1 | OnesaUCE themes look as they do in OneSauce's own frontend | Compare a few (Renee, Aeon Nox, Amiga Memories, Cafe80s) side by side; note anything missing or out of place | To check | Features implemented from the data only ([ONESAUCE-COMPAT.md](ONESAUCE-COMPAT.md)) |
| T2 | Menu videos stored as a folder behave as OneSauce does (random pick; B for another) | Renee → ARCADE | To check | The behaviour was inferred from the data (M20) |
| T3 | Cafe80s's handheld page text: does OneSauce show the three texts from `collections/_common/text`? | Compare 1 HANDHELDS in both frontends | To check | Open question from QA-ROUND10 |

## File and folder browser

| # | What to check | How | Status | Notes |
|---|---------------|-----|--------|-------|
| B1 | The browser works from a real controller: Select opens folders and picks, Back goes back up, Left/Right jump by letter | Manage → Systems → Add a system → Games folder, with a pad or an arcade encoder only | To check | Checked with posted keys only |
| B2 | "Browse with Windows..." requires a mouse click in Manage and recent use | On a cabinet with a trackball/spinner/light gun and no desktop mouse, move the controls in Manage: the row must stay absent. Click with a real mouse: it must appear. Wait 15 seconds or close/reopen Manage: it must disappear | To check | A device mouse-button press can also qualify |
| B3 | The Windows dialog from "Browse with Windows..." opens, returns its pick to the field, and gives the controls back afterwards | Click it with the mouse, pick a file, check the field; then press keys/buttons | To check | Not pressed in the GUI checks (it would open on the desktop) |
| B4 | Drives list without delay or system pop-ups: an empty card reader, a disc drive, a disconnected network share, a USB stick plugged in while RetroFE runs | Open the browser's first screen in each case | To check | Drive letters appear first; unanswered removable drives disappear; details are cached for 30 s; repeated visits share in-flight fetches; closing must not wait |
| B5 | Letter up / letter down jump in results lists when mapped (e.g. OneSauce's `letterUp = M,joyRightShoulder`) | A big folder in the browser, or the Games results | To check | |
