# qindaqt-kwin

QindaQt's compositor: KWin 6.6.6 plus QindaQt's own changes, with an identity
of its own (container-wm ADR-0289). This file is the map of what QindaQt
changed and why. The upstream base is recorded in [UPSTREAM.md](UPSTREAM.md).

## QindaQt changes

`git log --grep '^qindaqt:'` lists them. At the fork's creation:

| Change | Why | ADR |
|---|---|---|
| Decoration cutout passes pointer input through | the corner tab leaves part of the title row transparent | 0277 |
| Tablet proximity continues past the decoration | a pen entering range over a title bar reached no application | 0287 |
