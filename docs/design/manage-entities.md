# Design: Manage screens for games, menus, systems, videos, emulators and themes

Status: draft, round 1 in progress. Decisions and open questions are tracked
in [../DECISIONS.md](../DECISIONS.md); what each round delivered is in
[../FORK-CHANGELOG.md](../FORK-CHANGELOG.md).

## Goal

Manage becomes a hub for the things a RetroFE install is made of:

| Entry | What it manages |
|-------|-----------------|
| **Games** | Every installed game, searchable; a game's details (metadata, media, launcher, playlists) |
| **Menus** | The top menu (Browse) and every menu below it: menus of menus and sub-collections |
| **Systems** | Collections of games: where the games are, which extensions, which emulator, media |
| **Videos** | Collections of videos instead of games |
| **Emulators** | Installed emulators and the launchers that start them |
| **Themes** | Installed layouts; opens the theme editor (ported from One Saucier) |

The functionality is modelled on One Saucier's Manage and details screens,
adapted to RetroFE's layout-drawn forms: where One Saucier shows a details
screen as scrollable tiles, here each tile is a **link** row that opens its
own screen ([DECISIONS M2](../DECISIONS.md)).

Out of scope for now: packaging customizations and custom packs. The Catalog
gets one more source: this RetroFE fork itself, from GitHub.

## Building blocks (round 1)

### Navigation

A stack of screens. Each screen is a form id plus parameters (e.g.
`game=Arcade/pacman`); links and result rows push screens, Back pops them and
restores the previous selection. Actions can push a screen or go back after
they succeed.

### Providers

Each entity is a **provider** serving bindings under its own prefix
(`game:title`, actions `game.save`, result rows `game:search`, options
`game:genres`) and reading the current screen's parameters. Providers are
separate files registered in one place, so entity screens can be built in
parallel.

### New form elements

| Element | Use |
|---------|-----|
| `results` | Selectable rows from a search; Select opens a details screen |
| `link` | A row that opens another screen, with a status on the right |
| `value` | A read-only label and value |
| `image` | Artwork or other media from a path |
| `text live="yes"` | Search boxes that filter while typing |

### Data

A shared **library** layer reads what RetroFE reads (collections, menus,
items and metadata, media folders, launchers, layouts) and writes changes the
way RetroFE expects them, preserving comments and formatting as
`SettingsFile` already does. Indexing runs off the UI thread.

## Entities

*To be filled in from the One Saucier and RetroFE research (round 1).*

## Catalog: this fork from GitHub

There are no releases yet: CI uploads build artifacts only. The plan:

1. A `github` source type that reads a repository's releases
   (`https://api.github.com/repos/<owner>/<repo>/releases`) and offers each
   platform's release asset as a package of kind `app`.
2. The Sources screen offers "RetroFE (this fork)" as a one-step source for
   `ennisj/RetroFE`, alongside adding any other source.
3. Installing an `app` package updates RetroFE itself. Windows can't overwrite
   a running exe, so the update is staged and applied by renaming the running
   files aside and restarting; leftovers are removed on the next start.
4. A release workflow that builds and attaches the Windows package when a tag
   is pushed. Publishing a release is your call (see DECISIONS).

## Rounds

| Round | Work (parallel where listed together) |
|-------|---------------------------------------|
| 1 | Forms foundation (navigation, providers, new elements, Manage hub) · Aeon Nox as the default layout · library data layer · this plan |
| 2 | Games · Menus and Systems · Emulators/launchers · Videos · Catalog GitHub source and self-update · Aeon Nox styling of the new elements |
| 3 | Themes list and theme editor port · media replacement and other editing extras · end-to-end checks |

Every round: implement → adversarial review (Fable) → fixes → merge into
`feature/management-ui` → build, tests, deploy to `E:\Storage\OneSauce` →
changelog and decisions updated.
