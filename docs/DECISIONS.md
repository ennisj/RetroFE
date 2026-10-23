# Decisions and open items

Decisions made while building the fork, mostly without waiting for review.
Each one lists the options considered, what was chosen and why, so any of
them can be revisited. **Open** items still need your input; everything else
is decided but can be changed. Newest rounds are added at the end of each
section.

Status: **Decided** (chosen, can be revisited), **Open** (needs your input),
**Deferred** (agreed to leave for later).

---

## Layout and look

### L1. Where Aeon Nox comes from — Decided
- Options: the copy in OnesaUCE/One Saucier installs; RetroFE's original
  repository (phulshof/RetroFE); the FunKey-Project fork.
- Chosen: **phulshof/RetroFE**, folder `Package/Environment/Common/layouts/Aeon Nox`,
  commit `9f9230d09cbd4dae074b9465ca090567fd0623e7` (2023-04-15).
- Why: you asked for the official version. It's the upstream RetroFE copy,
  licensed with RetroFE (GPL-3.0), with the author's credits intact.

### L2. Aeon Nox is the default layout, shipped with the build — Decided
- Options: (a) keep the built-in layout as the default and offer Aeon Nox
  through the Catalog; (b) ship Aeon Nox in `layouts/` with the build and make
  it the default (`layout = Aeon Nox` in the built-in settings).
- Chosen: **(b)**. Without a `layouts/Aeon Nox` folder RetroFE still falls
  back to the built-in layout, so a truly bare exe keeps working.
- Why: you asked for Aeon Nox as the default. Shipping it beside the exe is
  part of the build, not a downloaded CoinOPS build, so the "runs bare" goal
  still holds.
- Consequence: the test install `E:\Storage\OneSauce` now gets
  `layouts\Aeon Nox` copied alongside `retrofe\` when builds are deployed. It
  stays otherwise bare (no settings, collections or catalog).

### L3. The Arcades layout stays in the repository — Decided
- It's upstream CoinOPS's default and harmless to keep; removing it would make
  merging upstream harder. It's no longer the default. Its Scout fonts and
  manufacturer logos have unclear redistribution terms, so if the fork ever
  publishes its own packages it may be worth dropping it from them (see L4).

### L4. Fonts and artwork with unclear licences — Open
- Aeon Nox's `MC360.ttf` (an Xbox 360-style font) and Arcades' Scout fonts and
  manufacturer logos have no clear licence. They're fine for local use.
- Recommendation: before publishing release packages, replace MC360 with an
  OFL font and leave Arcades out of the packages. Your call on timing.

---

## Manage screens

### M1. "Collections" in the request is named "Menus" in the UI — Decided
- Your list said *Collections*; your description called the same thing
  *Menus* (the top menu, sub-collections and menus of menus).
- Options: "Collections", "Menus", "Menus & Collections".
- Chosen: **Menus**. In RetroFE every list is a "collection", including
  systems, so "Collections" would blur Menus and Systems.

### M2. Detail screens drill down with links instead of tiles — Decided
- One Saucier shows a details screen as scrollable tiles. Here a details
  screen is a form; each tile becomes a **link** row (with a short status on
  the right, e.g. "Media — 4 of 7") that opens its own screen. Back returns.
- Why: your suggestion; it works with any controller and any layout, and each
  sub-screen can be styled separately.

### M3. One navigation stack for all screens — Decided
- Screens open on top of each other (Manage → Games → a game → Media) and
  Back returns to the previous one with its selection kept. F10 still opens
  Settings from anywhere.

---

## Working method

### W1. Parallel agents work in git worktrees — Decided
- Each agent works on its own branch in a separate worktree, builds and tests
  there, and commits. Branches are merged into `feature/management-ui` after a
  Fable adversarial review and fixes. Nothing is merged to `master`.
- GUI testing happens in private test roots under `E:\Storage\Agents\<name>\`
  so agents don't disturb `E:\Storage\OneSauce` or each other.

### W2. The fork's changelog is separate from upstream's — Decided
- Upstream's `CHANGELOG.md` is left alone so upstream merges stay clean. The
  fork's rounds are summarized in [FORK-CHANGELOG.md](FORK-CHANGELOG.md).
