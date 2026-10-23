# Fork changelog

What changed in this fork of CoinOPS RetroFE, round by round. Upstream's
changes are in [../CHANGELOG.md](../CHANGELOG.md). Open questions and the
reasoning behind choices are in [DECISIONS.md](DECISIONS.md).

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
