# Upstream record

qindaqt-kwin keeps KWin's history. Every upstream change enters through a merge
of an upstream release tag and gets a row here in the same change.

| Date | Upstream tag | Tag object | Commit | How it entered |
|---|---|---|---|---|
| 2026-09-28 | `v6.6.6` | `43cb730ca363b995dfd5f0ceb537e4c37a7bb5ff` | `9bf2235fad10de9048c634e376bf12e56b3023e6` | fork base (tree `88f96f8cde49c51552d82f60fd461b6e8b950685`) |

Upstream remote: `https://invent.kde.org/plasma/kwin.git`.

## Vendored kdecoration

`kdecoration/` is a squashed `git subtree` of KDE's kdecoration. Update it with
`git subtree pull --prefix=kdecoration https://invent.kde.org/plasma/kdecoration.git <tag> --squash`
and add a row here.

| Date | Upstream tag | Tag object | Commit | How it entered |
|---|---|---|---|---|
| 2026-09-28 | `v6.6.6` | `cb4988d3a403f383bbbb0fa25ea577c0a6a39b6f` | `80b838cec6ebc8fe7dbb2d5a2d3e464059835c30` | subtree add, squashed (tree `9b042b9070bdb6a38851c8ac49fe5fe55d951dd5`) |
