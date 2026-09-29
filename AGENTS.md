# qindaqt-kwin agent instructions

This repository is **qindaqt-kwin**, QindaQt's compositor: a fork of KDE's KWin
6.6 that is renamed from top to bottom so it installs and runs beside an
unmodified KDE Plasma, and an unmodified Plasma never reads its files. The
decision and its consequences are container-wm ADR-0289; the program of work is
container-wm `docs/plans/2026-09-28-plasma-free-qindaqt.md`. The QindaQt
compositor plugin, window decoration, tabbox and session stay in container-wm.

Read [`qindaqt/README.md`](qindaqt/README.md) before changing anything.

## Where it lives

- Hub: `qinda:~/git/qindaqt-kwin.git`. The working checkout
  `qinda:~/work_SPaC3/qindaqt-kwin` belongs to the manager; implement in a
  worktree on your own branch.
- Packaged by the QindaGentoo overlay as `gui-wm/qindaqt-kwin`, from a
  `git archive` of an exact hub commit.

## Commits

- A QindaQt change is a small, self-contained commit whose subject starts with
  `qindaqt:`. Name the governing container-wm ADR in a `QindaQt-ADR:` trailer.
  `git log --grep '^qindaqt:'` must list every downstream change.
- Never rewrite upstream history. Upstream fixes arrive only as a merge of an
  upstream release tag, recorded in [`qindaqt/UPSTREAM.md`](qindaqt/UPSTREAM.md)
  with the tag object and commit. Stay on the 6.6 line until a move is planned.

## Identity rules

- AGENT-GUARD: no installed path may equal one that stock `kde-plasma/kwin`,
  `kde-plasma/kwin-x11` or `kde-plasma/kdecoration` installs. A collision makes
  Portage refuse one of the two packages, which breaks the co-installation
  promise of ADR-0289.
- The C++ namespaces `KWin::` and `KDecoration3::` stay: the fork and stock
  libraries never load into one process, and keeping them keeps merges small.
- Keep everything QindaQt adds that is not an edit of an upstream file under
  `qindaqt/`, so upstream merges never meet it.

## Verification

Build with the commands in `qindaqt/README.md`, run the focused autotests for
what you changed, and report the commands and their results.
