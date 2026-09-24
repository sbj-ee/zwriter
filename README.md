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

Working **v1 feature core** on a dark distraction-free `QTextEdit` surface.

### Implemented now

- **Hide-away formatting toolbar** (FocusWriter-style — not a permanent ribbon): bold, italic, paragraph / H1 / H2 / H3; reveal on mouse near top/bottom edge or Esc; hides again when the pointer leaves / after a short idle
- **Hide-away File menu**: Open / Save / Save As / Export PDF / Print / Print Preview / Page Setup / Properties / Page Guides
- **Native save default: ODT**; also open/save **TXT** and best-effort **RTF** (no proprietary `.zwriter`, no DOCX in v1)
- **Export PDF…** (export-only — not a native edit/save format) via `QPrinter` PdfFormat
- **Print options** (lean): native OS print dialog, page setup (paper / orientation / margins), print preview
- **Page guides** toggle (Ctrl+G) — simple column margin guides, not a Word ruler
- **Bottom status bar** with live **word count** and **character count** (hide-away with chrome)
- **Document Properties** (Author, Created, Last edit); in-memory always; **ODT meta.xml** round-trip best-effort (patch after `QTextDocumentWriter`; last edit auto-updates on save; author defaults from `$USER` / system account)
- Esc chrome pin/unpin, F11 fullscreen
- Optional typewriter key-sound toggle (**default off**)
- Typewriter icon branding
- CI builds on Linux amd64 and macOS arm64

### Still roadmap / known limits

- Themes pack, autosave + restore cursor, multi-document / sessions
- Daily word / time goal, spell-check, scene / chapter navigation
- Richer ODT/RTF style round-trip; RTF is plain-text-oriented best-effort
- ODT Properties: body save is real; metadata is patched via `unzip`/`zip` into `meta.xml` (requires those tools). If patch fails, body still saves and a status message notes it
- Packaging (`.deb` / `.dmg`) — stubbed in `cmake/Packaging.cmake`
- Status extras (pages / paragraphs) — later; v1 is words + chars only

## v1 IN

- Fullscreen / hide-away chrome
- **Hide-away formatting toolbar** (bold / italic / headings)
- **Bottom status bar** with live word + character counts
- **Export PDF** (export-only)
- **Print** + page setup + print preview (lean; no Word-style advanced print UI)
- **Page guides** toggle
- **Document Properties** (author / created / last edit; ODT metadata best-effort)
- **ODT** native default save; **TXT** + basic **RTF** open/save
- **Optional typewriter key sounds** (toggle, default **off**; swappable sample pack)
- **Typewriter icon** branding
- Themes
- Autosave + restore cursor
- Multi-document / sessions
- Daily word / time goal
- Spell-check
- Scene / chapter navigation
- Optional typewriter scroll

## v1 OUT

- Mail merge / booklet / watermark print theater
- Track-changes
- SmartArt
- Template marketplace
- Cloud collab
- Ribbon UI
- Plugin marketplace
- Proprietary `.zwriter` format
- DOCX (not required for v1)
- Full Word Document Properties dump

## Formats

| Role | Formats |
|---|---|
| **Native default save** | ODT (OpenDocument Text) |
| **Also open / save** | TXT, basic RTF (best-effort) |
| **Export only** | PDF |
| **Not in v1** | DOCX, proprietary `.zwriter` |

ODT open uses `unzip` to read `content.xml` / `meta.xml` (Linux + macOS). Prefer ODT for fidelity; RTF is readable interchange.

## Build

Platforms: **Linux amd64** and **Apple Silicon (arm64)** only. No Windows, no
Intel Mac.

### Linux

```bash
sudo apt-get update
sudo apt-get install -y qt6-base-dev cmake ninja-build g++ unzip zip
# optional, for live key sounds:
# sudo apt-get install -y qt6-multimedia-dev

cmake -B build -G Ninja
cmake --build build
./build/zwriter
```

`qt6-base-dev` already pulls in Widgets + PrintSupport (PDF export + print).

### macOS (Apple Silicon)

```bash
brew install qt cmake ninja
cmake -B build -G Ninja
cmake --build build
./build/zwriter
```

`CMakeLists.txt` forces `CMAKE_OSX_ARCHITECTURES=arm64` on Darwin before
`project()` (same pattern as zedit). Multimedia remains optional.

### Shortcuts

| Key | Action |
|---|---|
| `Esc` | Pin / unpin hide-away chrome (menu, toolbar, status) |
| Mouse near top / bottom | Temporarily reveal hide-away chrome |
| `F11` | Fullscreen |
| `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` | Open / Save / Save As (default filter ODT) |
| `Ctrl+B` / `Ctrl+I` | Bold / italic |
| `Ctrl+G` | Toggle page guides |
| `Ctrl+Shift+E` | Export PDF… |
| `Ctrl+P` | Print… |
| `Ctrl+Shift+K` | Toggle typewriter key sounds (default off) |

## License

MIT — Copyright (c) 2026 Stephen B. Johnson. See [LICENSE](LICENSE).

## Icon

Classic **typewriter** silhouette (`assets/icons/zwriter.svg` /
`zwriter-128.png`) — not a Word-like W, not a fountain pen.
