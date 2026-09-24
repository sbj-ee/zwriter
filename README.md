# zwriter

![zwriter](assets/icons/zwriter-128.png)

**Distraction-free writing** for Linux amd64 and Apple Silicon. A sibling to
[zedit](https://github.com/sbj-ee/zedit) (code editor) — not a fork — with
kinship to [FocusWriter](https://gottcode.org/focuswriter/) packaging and
vibe. Built with **C++20 + Qt 6 Widgets**.

Public repository: https://github.com/sbj-ee/zwriter

## Positioning

| | Role |
|---|---|
| **zwriter** | Long-form prose. Fullscreen hide-away chrome, themes, goals, scenes. |
| **zedit** | Code editor (syntax, LSP-minded). Different product, shared author. |
| **FocusWriter** | Inspiration for distraction-free writing UX / packaging. |
| **Word** | OUT — bureaucracy, ribbon, mail merge, track-changes theater. |

## Status

First-commit **skeleton**: a dark, readable `QMainWindow` + `QTextEdit` surface
with live word/char count, Esc chrome toggle, F11 fullscreen, optional
typewriter key-sound toggle (default off), and CI that **really builds** on
Linux and macOS arm64. Packaging (`.deb` / `.dmg`) is stubbed.

## v1 roadmap

### IN

- Fullscreen / hide-away chrome
- Themes
- Rich text (bold / italic / headings)
- TXT + basic ODT (+ RTF if easy)
- Autosave + restore cursor
- Multi-document / sessions
- Live word / char stats
- Daily word / time goal
- Spell-check
- Scene / chapter navigation
- Optional typewriter scroll
- **Optional typewriter key sounds** (toggle, default **off**; swappable sample pack)

### OUT

- Mail merge
- Track-changes
- SmartArt
- Template marketplace
- Cloud collab
- Ribbon UI
- Plugin marketplace

## Build

Platforms: **Linux amd64** and **Apple Silicon (arm64)** only. No Windows, no
Intel Mac.

### Linux

```bash
sudo apt-get update
sudo apt-get install -y qt6-base-dev cmake ninja-build g++
# optional, for live key sounds:
# sudo apt-get install -y qt6-multimedia-dev

cmake -B build -G Ninja
cmake --build build
./build/zwriter
```

### macOS (Apple Silicon)

```bash
brew install qt cmake ninja
cmake -B build -G Ninja
cmake --build build
./build/zwriter
```

`CMakeLists.txt` forces `CMAKE_OSX_ARCHITECTURES=arm64` on Darwin before
`project()` (same pattern as zedit).

### Shortcuts (skeleton)

| Key | Action |
|---|---|
| `Esc` | Toggle thin status chrome |
| `F11` | Fullscreen |
| `Ctrl+Shift+K` | Toggle typewriter key sounds (default off) |

## License

MIT — Copyright (c) 2026 Stephen B. Johnson. See [LICENSE](LICENSE).

## Icon

Classic **typewriter** silhouette (`assets/icons/zwriter.svg` /
`zwriter-128.png`) — not a Word-like W, not a fountain pen.
