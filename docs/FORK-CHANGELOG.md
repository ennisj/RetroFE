# Fork changelog

What changed in this fork of CoinOPS RetroFE, round by round. Upstream's
changes are in [../CHANGELOG.md](../CHANGELOG.md). Open questions and the
reasoning behind choices are in [DECISIONS.md](DECISIONS.md).

## Round 1 — foundations (2026-10-23)

Five agents in parallel, each followed by a Fable adversarial review and a
round of fixes before merging. Unit tests: 327 → 2,766 checks.

- **Aeon Nox is the default layout.** The official copy from RetroFE's
  original repository, adapted: a games page, a systems page for every menu
  (new theme-wide `collections/_menu/` page, decision L5), Home and Catalog
  pages with drawn logos and icons, styled Settings/Manage/Catalog screens and
  on-screen keyboard, and 4:3 versions of everything (screens now follow the
  layout variant, L6). Fonts with missing or non-redistributable licences were
  replaced; NOTICE.txt records provenance (open questions in L4).
- **Manage hub and form foundation.** Screens stack (Back returns with the
  selection kept); entity sections plug in as providers in their own folders;
  new form elements: `results` (searchable lists with thumbnails), `link`,
  `value`, `image`, live text; confirm-before-acting buttons; unsaved-changes
  warnings; a deferred reload when leaving Manage. The hub lists Games,
  Menus, Systems, Videos, Emulators and Themes (placeholders until round 2).
- **Library.** A shared index of collections, menus, systems, games,
  metadata, launchers, media and layouts, built off the UI thread (30,000
  games on the OnesaUCE test data in about half a second warm), with search,
  media and launcher resolution the way RetroFE does it, and validated,
  backed-up writes (metadata edits go to a per-user override layer, M6).
  OnesaUCE compatibility: `media.basePath`, comma launcher lists (first one
  wins) and `list.includeFile`/`excludeFile` are now honoured by RetroFE too.
- **Catalog: RetroFE from GitHub.** A `github` source type, a one-step "Add
  RetroFE from GitHub", and self-update: verified download (a checksum is
  required), backup, journaled swap of the program folder with recovery at
  the next start, restart. A release workflow builds and publishes when a
  version tag is pushed (not run yet — C1).
- **Theme editor core.** One Saucier's project model, 35 widgets (adapted to
  this engine), generator, layout checker and in-theme storage, ported with
  tests (also checked on Linux with g++).

## Before the rounds (branch `feature/management-ui`)

- **Bare boot.** RetroFE starts with nothing but its executable: built-in
  settings, controls, layout and font apply when files are missing.
- **Home menu.** Catalog, Settings, Manage and Browse; Browse opens the usual
  top menu. On by default for bare installs.
- **Forms.** Settings and Manage are XML forms bound to settings and other
  values, drawn as layout pages (`<form>` element) and driven by RetroFE's
  controls; an on-screen keyboard page (`<keyboard>`) for text.
- **Catalog.** Sources (an index file on the web or disk, or a folder of zip
  packages), package list and details, background installs with resume,
  verification, staged installs, manifests, backups and removal; hardened after
  an adversarial review (journaled installs with rollback, protected folders,
  no executables, Windows device names refused, same-source updates).
