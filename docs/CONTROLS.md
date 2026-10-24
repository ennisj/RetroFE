# Controls
[Back](README.md)

The `controls.conf` file, located in your RetroFE directory, contains the
controls for your RetroFE frontend. Multiple keys can be assigned to a
single action, separated by a `,`

Example:

 ```Up = Keypad 8, Up```

This will assign both the Keypad 8 (up arrow on your keypad) and the up
arrow to RetroFE's Up control.

You can also change them from inside RetroFE: **Settings > Controls** (see
[MANAGER.md](MANAGER.md#controls)) lists every control with its inputs and
adds one when you press it.

RetroFE reads `controls.conf`, then `controls1.conf` to `controls9.conf` on
top of it: a control set in a later file replaces the whole value from an
earlier one. Without a `controls.conf`, RetroFE uses its built-in controls
(arrows, Return, Escape, Q, Page Up/Down, and a pad's d-pad, left stick, A
and B). A layout
can add `controls - <type>.conf` for its pages (the layout's `controls`
attribute).

An input RetroFE doesn't recognise (a typo, or another program's name for
it) is skipped with a warning in the log; the control keeps its other
inputs. RetroFE never stops because of one.

## RetroFE Control Keys

### Essential
RetroFE always has these, unless you leave one empty on purpose:

- **Not set** in any file: Up, Down, Left and Right use their partner's
  inputs (Up for Left, Left for Up, Down for Right, Right for Down) as
  before; failing that, and for Select, Back and Quit, RetroFE's built-in
  default (below), with a warning in the log. (Upstream stopped instead.)
- **Set, but none of its inputs is one RetroFE knows** (a typo, another
  program's name): the built-in default, with a warning.
- **Set to nothing** (`quit =`, e.g. on a kiosk so nobody can leave): nothing
  is bound to it, as upstream; the log says so.

The built-in defaults are
`up = Up,joyDPUp,joyLeftY-,joyHat0Up,joyAxis1-` (and the same for the other
directions), `select = Return,joyA,joyUnmappedButton0`,
`back = Escape,joyB,joyUnmappedButton1` and `quit = Q`. A pad SDL has a
mapping for selects with A and goes back with B; a joystick it has no mapping
for (an arcade encoder such as a Zero Delay) selects with button 0 and goes
back with button 1, as RetroFE always did. Buttons 0 and 1 are written
`joyUnmapped...` there because on some mapped pads (a DualShock read through
DirectInput) button 1 is A, which would make one button both Select and Back.
| Control | Description |
|---------|-------------|
| `up` | Scrolls menu up (for vertical menus) |
| `down` | Scrolls menu down (for vertical menus) |
| `left` | Scrolls menu left (for horizontal menus) |
| `right` | Scrolls menu right (for horizontal menus) |
| `select` | Selects the active menu item |
| `back` | Leaves current menu |
| `quit` | Exits the frontend. Closing the window (title-bar X, Alt+F4) does the same, asking Manage's screens about unsaved changes first if one is open (closing again leaves, dropping them); it does nothing in kiosk mode or when `quit` is bound to nothing. |
| `manager` | Opens Settings (default F10). Keyboard keys only: the first one listed is used, and F10 when it lists none. |

The Manage screens use `up`, `down`, `left`, `right`, `select` and `back`
too, and the theme editor (Manage > Themes > Edit) adds `pageUp` / `pageDown`
(the previous / next region of its screen) and `letterUp` / `letterDown` (the
previous / next widget on the canvas). See
[THEME_EDITOR.md](THEME_EDITOR.md#with-a-controller).

### Basic Navigation
Most users will want to use these keys to move around a build
| Control | Description |
|---------|-------------|
| `nextCyclePlaylist` | Switches to the next playlist in the cyclePlaylist set. |
| `prevCyclePlaylist` | Switches to the previous playlist in the cyclePlaylist set. |
| `letterUp` | Scrolls menu to the previous item in the alphabet |
| `letterDown` | Scrolls menu to next item in the alphabet |

### Advanced Navigation
| Control | Description |
|---------|-------------|
| `playlistUp` | Scrolls to the previous playlist in a collection in vertical layouts. |
| `playlistDown` |Scrolls to the next playlist in a collection in vertical layouts. |
| `playlistLeft` | Scrolls to the previous playlist in a collection in horizontal layouts. |
| `playlistRight` | Scrolls to the next playlist in a collection in horizontal layouts. |
| `collectionUp` | Scrolls to the previous collection in vertical layouts. Will enter that collection if enterOnCollection is true. |
| `collectionDown` | Scrolls menu next collection in vertical layouts. Will enter that collection based if enterOnCollection is true. |
| `collectionLeft` | Scrolls to the previous collection in horizontal layouts. Will enter that collection if enterOnCollection is true. |
| `collectionRight` | Scrolls menu next collection in horizontal layouts. Will enter that collection based if enterOnCollection is true. |
| `pageUp` | Scrolls menu back by a page |
| `pageDown` | Scrolls menu forward by a page |
| `prevPlaylist` | Switches to the previous playlist. |
| `nextPlaylist` | Switches to the next playlist. |
| `cyclePlaylist` | Switches to the next playlist in the cyclePlaylist set. Still functional, but has been replaced by nextCyclePlaylist. |
| `prevCycleCollection` | Switches to the previous playlist in the cycleCollection set. |
| `cycleCollection` | Switches to the next playlist in the cycleCollection set. |

### Jukebox Mode
These keys are used to define jukebox controls when RetroFE operates in a jukebox environment
| Control | Description |
|---------|-------------|
| `jbFastForward1m` | Jukebox fast forward 1 minute |
| `jbFastRewind1m` | Jukebox fast rewind 1 minute |
| `jbFastForward5p` | Jukebox fast forward 5% |
| `jbFastRewind5p` | Jukebox fast rewind 5% |
| `jbPause` | Jukebox pause |
| `jbRestart` | Jukebox restart |

### Button Combos
These keys are used to define button combos with two keycodes. The first value defines the shift modifier
| Control | Description |
|---------|-------------|
| `quitCombo` | Quits RetroFE |
| `settingsCombo` | Switches to the settings playlist defined in settings.conf `controllerComboSettings` |
| `gameInfoCombo` | Toggles Game Info with a button combo |
| `collectionInfoCombo` | Toggles Collection Info with a button combo |
| `buildInfoCombo` | Toggles Build Info with a button combo |

### Favorites
| Control | Description |
|---------|-------------|
| `addPlaylist` | Adds current item to the favorites playlist |
| `removePlaylist` | Removes current item from the favorites playlist |
| `favPlaylist` | Switches to the favorites playlist |
| `togglePlaylist` | Toggle adding current item to favorites playlist |
| `random` | Selects a random item |
| `quickPlaylist` | Jumps to playlist defined at /collections/*/quicklist |

### Admin
| Control | Description |
|---------|-------------|
| `settings` | Switches to the settings playlist |
| `reboot` | Reboot RetroFE and refresh config |
| `kiosk` | Toggle kiosk mode |
| `saveFirstPlaylist` | Saves current playlist as `firstPlaylist` in settings_saved.conf |

### Deadzone
| Control | Description |
|---------|-------------|
| `deadZone` | Defines the dead zone for analog inputs between 0 and 100, with 100 blocking all analog input. Sticks and triggers count as pressed once past it (default 3 when `controls.conf` doesn't set it) |

### Other
| Control | Description |
|---------|-------------|
| `menu` | Opens the menu |

## Game controller codes

Pads that SDL knows as a game controller (Xbox, PlayStation, Switch Pro and
most others, through SDL's controller mappings) can be named by button, the
same button whatever the pad. These are SDL's game controller names, with
`joy` in front:

| Keycode | Description |
|---------|-------------|
| `joyA`, `joyB`, `joyX`, `joyY` | The face buttons (Xbox lettering: A bottom, B right, X left, Y top) |
| `joyBack`, `joyGuide`, `joyStart` | Back (Select, Share), the guide button (Xbox, PS), Start (Menu, Options) |
| `joyLeftShoulder`, `joyRightShoulder` | LB / L1, RB / R1 |
| `joyLeftStick`, `joyRightStick` | Pressing a stick in (L3, R3) |
| `joyDPUp`, `joyDPDown`, `joyDPLeft`, `joyDPRight` | The d-pad |
| `joyMisc1`, `joyPaddle1` to `joyPaddle4`, `joyTouchpad` | Extra buttons some pads have |
| `joyLeftX-`, `joyLeftX+`, `joyLeftY-`, `joyLeftY+` | Left stick left, right, up, down |
| `joyRightX-`, `joyRightX+`, `joyRightY-`, `joyRightY+` | Right stick left, right, up, down |
| `joyLeftTrigger`, `joyRightTrigger` | LT / L2, RT / R2 (a `+` may follow; `-` isn't allowed) |

Like the joystick codes below, `joy0A` names the first pad connected and
`joyA` any pad. Names aren't case-sensitive. A stick needs `+` or `-`; a
button can't have one. A key whose name has a comma in it (`Keypad ,`)
can't be used, as the comma separates inputs; the `,` key itself can (an
empty entry stands for it: `random = ,R`). Pads plugged in while RetroFE runs work straight away.

## Gamepad/Joystick codes

For joysticks SDL has no mapping for (arcade encoders, some sticks), or to
name a raw button number:

- `X` = Controller number (e.g. `joy0` is the first connected gamepad)
- `Y` = Button or hat number on controller `X`

| Keycode| Description |
|--------|-------------|
| `joyXButtonY` | button Y on joystick X |
| `joyXHatYLeftUp` | hat (D-pad) diagonally up-left (hat Y on joystick X) |
| `joyXHatYLeft` | hat (D-pad) left direction (hat Y on joystick X) |
| `joyXHatYLeftDown` | hat (D-pad) diagonally down-left (hat Y on joystick X) |
| `joyXHatYUp` | hat (D-pad) up direction (hat Y on joystick X) |
| `joyXHatYDown` | hat (D-pad) down direction (hat Y on joystick X) |
| `joyXHatYRightUp` | hat (D-pad) diagonally up-right (hat Y on joystick X) |
| `joyXHatYRight` | hat (D-pad) right direction (hat Y on joystick X) |
| `joyXHatYRightDown` | hat (D-pad) diagonally down-right (hat Y on joystick X) |
| `joyXAxis0+` | Left analog stick, horizontal axis right (joystick X) |
| `joyXAxis0-` | Left analog stick, horizontal axis left (joystick X) |
| `joyXAxis1+` | Left analog stick, vertical axis down (joystick X) |
| `joyXAxis1-` | Left analog stick, vertical axis up (joystick X) |
| `joyXAxis2+` | Right analog stick, horizontal axis right (joystick X) |
| `joyXAxis2-` | Right analog stick, horizontal axis left (joystick X) |
| `joyXAxis3+` | Right analog stick, vertical axis down (joystick X) |
| `joyXAxis3-` | Right analog stick, vertical axis up (joystick X) |
| `joyXUnmappedButtonY`, `joyXUnmappedHatY...`, `joyXUnmappedAxisY+` | The same inputs, but only on joysticks SDL has no game controller mapping for (arcade encoders); a pad that has one ignores them. The built-in defaults use them; `joyButtonY` itself counts on every joystick, as it always did. |

If X is omitted, RetroFE will accept input from all controllers.
(version 0.8.13+) An axis needs `+` or `-`.

A pad with a controller mapping sends both kinds of events, so `joyA` and
`joyButton0` both work on an Xbox pad (Settings > Controls adds the
controller name when you press one).

## Mouse Codes

| Keycode | Description |
|---------|-------------|
| `mouseButtonLeft` | the left mouse button |
| `mouseButtonMiddle` | the middle mouse button |
| `mouseButtonRight` | the right mouse button |
| `mouseButtonX1` | the X1 mouse button |
| `mouseButtonX2` | the X2 mouse button |

 
## Keyboard Codes

| Keycode | Description |
|---------|-------------|
| 0 | |
| 1 | |
| 2 | |
| 3 | |
| 4 | |
| 5 | |
| 6 | |
| 7 | |
| 8 | |
| 9 | |
| A | |
| AC Back | the Back key (application control keypad) |
| AC Bookmarks | the Bookmarks key (application control keypad) |
| AC Forward | the Forward key (application control keypad) |
| AC Home | the Home key (application control keypad) |
| AC Refresh | the Refresh key (application control keypad) |
| AC Search | the Search key (application control keypad) |
| AC Stop | the Stop key (application control keypad) |
| Again | the Again key (Redo) |
| AltErase | Erase-Eaze |
| ' | |
| Application | the Application / Compose / Context Menu (Windows) key |
| AudioMute | the Mute volume key |
| AudioNext | the Next Track media key |
| AudioPlay | the Play media key |
| AudioPrev | the Previous Track media key |
| AudioStop | the Stop media key) |
| B | |
| \\ | Located at the lower left of the return key on ISO keyboards and at the right end of the QWERTY row on ANSI keyboards. Produces REVERSE SOLIDUS (backslash) and VERTICAL LINE in a US layout, REVERSE SOLIDUS and VERTICAL LINE in a UK Mac layout, NUMBER SIGN and TILDE in a UK Windows layout, DOLLAR SIGN and POUND SIGN in a Swiss German layout, NUMBER SIGN and APOSTROPHE in a German layout, GRAVE ACCENT and POUND SIGN in a French Mac layout, and ASTERISK and MICRO SIGN in a French Windows layout. |
| Backspace | |
| BrightnessDown | the Brightness Down key |
| BrightnessUp | the Brightness Up key |
| C | |
| Calculator | the Calculator key |
| Cancel | |
| CapsLock | |
| Clear | |
| Clear / Again | |
| , | |
| Computer | the My Computer key |
| Copy | |
| CrSel | |
| CurrencySubUnit | the Currency Subunit key |
| CurrencyUnit | the Currency Unit key |
| Cut | |
| D | |
| DecimalSeparator | the Decimal Separator key |
| Delete | |
| DisplaySwitch | display mirroring/dual display switch, video mode switch |
| Down | the Down arrow key (navigation keypad) |
| E | |
| Eject | the Eject key |
| End | |
| = | |
| Escape | the Esc key |
| Execute | |
| ExSel | |
| F | |
| F1 | |
| F10 | |
| F11 | |
| F12 | |
| F13 | |
| F14 | |
| F15 | |
| F16 | |
| F17 | |
| F18 | |
| F19 | |
| F2 | |
| F20 | |
| F21 | |
| F22 | |
| F23 | |
| F24 | |
| F3 | |
| F4 | |
| F5 | |
| F6 | |
| F7 | |
| F8 | |
| F9 | |
| Find | |
| G | |
| \` | Located in the top left corner (on both ANSI and ISO keyboards). Produces GRAVE ACCENT and TILDE in a US Windows layout and in US and UK Mac layouts on ANSI keyboards, GRAVE ACCENT and NOT SIGN in a UK Windows layout, SECTION SIGN and PLUS-MINUS SIGN in US and UK Mac layouts on ISO keyboards, SECTION SIGN and DEGREE SIGN in a Swiss German layout (Mac: only on ISO keyboards), CIRCUMFLEX ACCENT and DEGREE SIGN in a German layout (Mac: only on ISO keyboards), SUPERSCRIPT TWO and TILDE in a French Windows layout, COMMERCIAL AT and NUMBER SIGN in a French Mac layout on ISO keyboards, and LESS-THAN SIGN and GREATER-THAN SIGN in a Swiss German, German, or French Mac layout on ANSI keyboards. |
| H | |
| Help | |
| Home | |
| I | |
| Insert | insert on PC, help on some Mac keyboards (but does send code 73, not 117) |
| J | |
| K | |
| KBDIllumDown | the Keyboard Illumination Down key |
| KBDIllumToggle | the Keyboard Illumination Toggle key |
| KBDIllumUp | the Keyboard Illumination Up key |
| Keypad 0 | the 0 key (numeric keypad) |
| Keypad 00 | the 00 key (numeric keypad) |
| Keypad 000 | the 000 key (numeric keypad) |
| Keypad 1 | the 1 key (numeric keypad) |
| Keypad 2 | the 2 key (numeric keypad) |
| Keypad 3 | the 3 key (numeric keypad) |
| Keypad 4 | the 4 key (numeric keypad) |
| Keypad 5 | the 5 key (numeric keypad) |
| Keypad 6 | the 6 key (numeric keypad) |
| Keypad 7 | the 7 key (numeric keypad) |
| Keypad 8 | the 8 key (numeric keypad) |
| Keypad 9 | the 9 key (numeric keypad) |
| Keypad A | the A key (numeric keypad) |
| Keypad & | the & key (numeric keypad) |
| Keypad @ | the @ key (numeric keypad) |
| Keypad B | the B key (numeric keypad) |
| Keypad Backspace | the Backspace key (numeric keypad) |
| Keypad Binary | the Binary key (numeric keypad) |
| Keypad C | the C key (numeric keypad) |
| Keypad Clear | the Clear key (numeric keypad) |
| Keypad ClearEntry | the Clear Entry key (numeric keypad) |
| Keypad : | the : key (numeric keypad) |
| Keypad , | the Comma key (numeric keypad) |
| Keypad D | the D key (numeric keypad) |
| Keypad && | the && key (numeric keypad) |
| \\\| | \\\| key (numeric keypad) |
| Keypad Decimal | the Decimal key (numeric keypad) |
| Keypad / | the / key (numeric keypad) |
| Keypad E | the E key (numeric keypad) |
| Keypad Enter | the Enter key (numeric keypad) |
| Keypad = | the = key (numeric keypad) |
| Keypad = (AS400) | the Equals AS400 key (numeric keypad) |
| Keypad ! | the ! key (numeric keypad) |
| Keypad F | the F key (numeric keypad) |
| Keypad \> | the Greater key (numeric keypad) |
| Keypad # | the # key (numeric keypad) |
| Keypad Hexadecimal | the Hexadecimal key (numeric keypad) |
| Keypad { | the Left Brace key (numeric keypad) |
| Keypad ( | the Left Parenthesis key (numeric keypad) |
| Keypad \< | the Less key (numeric keypad) |
| Keypad MemAdd | the Mem Add key (numeric keypad) |
| Keypad MemClear | the Mem Clear key (numeric keypad) |
| Keypad MemDivide | the Mem Divide key (numeric keypad) |
| Keypad MemMultiply | the Mem Multiply key (numeric keypad) |
| Keypad MemRecall | the Mem Recall key (numeric keypad) |
| Keypad MemStore | the Mem Store key (numeric keypad) |
| Keypad MemSubtract | the Mem Subtract key (numeric keypad) |
| Keypad - | the - key (numeric keypad) |
| Keypad \\\* | the \\\* key (numeric keypad) |
| Keypad Octal | the Octal key (numeric keypad) |
| Keypad % | the Percent key (numeric keypad) |
| Keypad . | the . key (numeric keypad) |
| Keypad + | the + key (numeric keypad) |
| Keypad +/- | the +/- key (numeric keypad) |
| Keypad ^ | the Power key (numeric keypad) |
| Keypad } | the Right Brace key (numeric keypad) |
| Keypad ) | the Right Parenthesis key (numeric keypad) |
| Keypad Space | the Space key (numeric keypad) |
| Keypad Tab | the Tab key (numeric keypad) |
| key (numeric keypad) | |
| Keypad XOR | the XOR key (numeric keypad) |
| L | |
| Left Alt | alt, option |
| Left Ctrl | |
| Left | the Left arrow key (navigation keypad) |
| \[ | |
| Left GUI | windows, command (apple), meta |
| Left Shift | |
| M | |
| Mail | the Mail/eMail key |
| MediaSelect | the Media Select key |
| Menu | |
| \- | |
| ModeSwitch | I'm not sure if this is really not covered by any of the above, but since there's a special KMOD_MODE for it I'm adding it here |
| Mute | |
| N | |
| Numlock | the Num Lock key (PC) / the Clear key (Mac) |
| O | |
| Oper | |
| Out | |
| P | |
| PageDown | |
| PageUp | |
| Paste | |
| Pause the Pause / Break key | |
| . | |
| Power | The USB document says this is a status flag, not a physical key - but some Mac keyboards do have a power key. |
| PrintScreen | |
| Prior | |
| Q | |
| R | |
| Right Alt | alt gr, option |
| Right Ctrl | |
| Return | the Enter key (main keyboard) |
| Return | |
| Right GUI | windows, command (apple), meta |
| Right | the Right arrow key (navigation keypad) |
| \] | |
| Right Shift | |
| S | |
| ScrollLock | |
| Select | |
| ; | |
| Separator | |
| / | |
| Sleep | the Sleep key |
| Space | the Space Bar key(s) |
| Stop | |
| SysReq | the SysReq key |
| T | |
| Tab | the Tab key |
| ThousandsSeparator | the Thousands Separator key |
| U | |
| Undo | |
| Up | the Up arrow key (navigation keypad) |
| V | |
| VolumeDown | |
| VolumeUp | |
| W | |
| WWW | the WWW/World Wide Web key |
| X | |
| Y | |
| Z | |
| # | ISO USB keyboards actually use this code instead of 49 for the same key, but all OSes I've seen treat the two codes identically. So, as an implementor, unless your keyboard generates both of those codes and your OS treats them differently, you should generate SDL_SCANCODE_BACKSLASH instead of this code. As a user, you should not rely on this code because SDL will never generate it with most (all?) keyboards. |
| & | |
| \* | |
| @ | |
| ^ | |
| : | |
| $ | |
| ! | |
| \> | |
| # | |
| ( | |
| \< | |
| % | |
| \+ | |
| ? | |
| ) | |
| \_ | |

These codes were taken from <https://wiki.libsdl.org/SDL_Keycode>

[Back](README.md)
