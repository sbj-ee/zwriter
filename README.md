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

- **Hide-away formatting toolbar** (FocusWriter-style — not a permanent ribbon): **font family** (system `QFontComboBox`) + **font size** (pt spin), bold, italic, paragraph / H1 / H2 / H3; reveal on mouse near top/bottom edge or Esc; hides again when the pointer leaves / after a short idle; toolbar follows the caret’s current face/size
- **Paper-white page** (default theme) — near-white writing surface + dark typewriter text; dark hide-away chrome; View → Theme → Paper / Dark room (persisted in QSettings)
- **Full Page view** (default **on**) — centered paper page on a desk background, zoom-to-fit one page, margins from Page Setup / print geometry; View → Full Page (Ctrl+Shift+P); off = continuous strip; persisted in QSettings
- **Default body face: typewriter / Courier-class monospace** (`Courier New` → `Liberation Mono` → `Noto Sans Mono` → `Menlo` / `Monaco` → `DejaVu Sans Mono` → `monospace`); switch anytime via the font picker; ODT round-trip preserves face/size; print/PDF honor fonts
- **Hide-away File + View menus**: Open / Save / Save As / Recent / Export PDF / Print / Print Preview / Page Setup / Properties / Page Guides; View toggles for **Full Page**, typewriter scroll, focus mode, smart quotes, **Theme (Paper / Dark room)**
- **Polished native Save/Open/Export dialogs** (Qt `QFileDialog`: Documents sidebar, last-dir via QSettings, live suffix from filter, OS overwrite confirm, Create Directory/New Folder via native panel, titles “Save Document” / “Open Document” / “Export PDF”)
- **Native save default: ODT**; also open/save **TXT** and best-effort **RTF** (no proprietary `.zwriter`, no DOCX in v1)
- **Export PDF…** (export-only — not a native edit/save format) via `QPrinter` PdfFormat
- **Print options** (lean): native OS print dialog, page setup (paper / orientation / margins), print preview
- **Page guides** toggle (Ctrl+G) — simple column margin guides, not a Word ruler
- **Bottom status bar** with live **word count**, **character count**, and **reading time** (~N min at 225 WPM)
- **Document Properties** (Author, Created, Last edit); in-memory always; **ODT meta.xml** round-trip best-effort
- **Typewriter scrolling** (default **on**) — caret stays vertically centered while typing/navigating
- **Focus mode** — dim everything except the current paragraph or sentence (scope in View → Focus Scope)
- **Find / Replace** — keyboard-first bar (Ctrl+F / Ctrl+H); next/prev, replace, replace all; optional match case
- **Recent files** — File → Open Recent; persisted via QSettings; stale paths cleared
- **Smart quotes / dashes** (default **off**) — curly quotes and en/em dashes from ASCII while typing
- Esc chrome pin/unpin, F11 fullscreen
- Optional typewriter key-sound toggle (**default off**) — mechanical `key.wav` click + `return.wav` carriage; typing/Return only (not arrow navigation)
- Typewriter icon branding
- **Tables** (QTextTable): Insert → Table… / toolbar; Tab between cells; add/remove row or column; ODT + PDF/print
- **Help** menu: About zwriter (shows PROJECT_VERSION) + Check for Updates (GitHub releases/latest)
- CI builds + packages on Linux amd64 (`.deb`) and macOS arm64 (`.dmg`)

### Still roadmap / known limits

- Richer themes pack (beyond Paper / Dark room), autosave + restore cursor, multi-document / sessions
- Daily word / time goal, spell-check, scene / chapter navigation
- Richer ODT/RTF style round-trip; RTF is plain-text-oriented best-effort
- ODT Properties: body save is real; metadata is patched via `unzip`/`zip` into `meta.xml` (requires those tools). If patch fails, body still saves and a status message notes it
- macOS `.dmg` ships the binary (not a full `.app` + macdeployqt bundle yet)
- Status extras (pages / paragraphs) — later
- Full Page view is one fitted page frame (scroll inside); not multi-page WYSIWYG with inter-page gaps

## v1 IN

- Fullscreen / hide-away chrome
- **Hide-away formatting toolbar** (font family / size / bold / italic / headings)
- **Paper-white page** default theme (dark chrome); Dark room optional
- **Full Page view** (default on) — page-shaped canvas on desk; continuous strip when off
- **Typewriter / Courier-class default body font** (system monospace fallbacks); user-selectable
- **Bottom status bar** with live word + character counts + **reading time**
- **Export PDF** (export-only)
- **Print** + page setup + print preview (lean; no Word-style advanced print UI)
- **Page guides** toggle
- **Document Properties** (author / created / last edit; ODT metadata best-effort)
- **ODT** native default save; **TXT** + basic **RTF** open/save
- **Optional typewriter key sounds** (toggle, default **off**; bundled `assets/sounds/key.wav` + `return.wav` procedural placeholders; Qt Multimedia / QSoundEffect)
- **Typewriter icon** branding
- **Typewriter scrolling** (toggle, default **on**)
- **Focus mode** (sentence or paragraph scope)
- **Find and replace** (Ctrl+F / Ctrl+H)
- **Recent files** menu (QSettings)
- **Smart quotes / dashes** (toggle, default **off**)
- **Help → About** + **Check for Updates** (GitHub latest release)
- **Tables** (insert, edit cells, add/remove row/column; ODT round-trip)
- Themes (Paper default + Dark room; fuller packs later)
- Autosave + restore cursor
- Multi-document / sessions
- Daily word / time goal
- Spell-check
- Scene / chapter navigation

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
- Per-theme locked font packs / downloadable font store
- OpenType feature UI
- Word-style style gallery

## Formats

| Role | Formats |
|---|---|
| **Native default save** | ODT (OpenDocument Text) |
| **Also open / save** | TXT, basic RTF (best-effort) |
| **Export only** | PDF |
| **Not in v1** | DOCX, proprietary `.zwriter` |

ODT open uses `unzip` to read `content.xml` / `meta.xml` (Linux + macOS). Prefer ODT for fidelity; RTF is readable interchange.

## Version bumps

**One place only:** the `VERSION` argument of `project()` in the top-level
`CMakeLists.txt`. That value becomes `PROJECT_VERSION`, generates
`version.hpp` for the app, and feeds CPack artifact names. Do not hand-edit
a version header.

```cmake
project(zwriter VERSION 0.1.0 LANGUAGES CXX)  # example bump
```

Semver `MAJOR.MINOR.PATCH`. GitHub Release tags: `vX.Y.Z`. Artifacts:
`zwriter-X.Y.Z-Linux-amd64.deb`, `zwriter-X.Y.Z-Darwin.dmg`.

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

# package .deb (optional):
cd build && cpack -G DEB
```

`qt6-base-dev` already pulls in Widgets + PrintSupport (PDF export + print).

### macOS (Apple Silicon)

```bash
brew install qt cmake ninja
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/zwriter

# package .dmg (optional):
cd build && cpack -G DragNDrop
```

`CMakeLists.txt` forces `CMAKE_OSX_ARCHITECTURES=arm64` on Darwin before
`project()` (same pattern as zedit). Multimedia remains optional.

### Shortcuts

| Key | Action |
|---|---|
| `Esc` | Close find bar if open; else pin / unpin hide-away chrome |
| Mouse near top / bottom | Temporarily reveal hide-away chrome |
| `F11` | Fullscreen |
| `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` | Open / Save / Save As (default filter ODT) |
| `Ctrl+F` / `Ctrl+H` | Find / Replace |
| `F3` / `Shift+F3` | Find next / previous |
| `Ctrl+B` / `Ctrl+I` | Bold / italic |
| `Ctrl+G` | Toggle page guides |
| `Ctrl+Shift+P` | Toggle Full Page view (default on) |
| `Ctrl+Shift+E` | Export PDF… |
| `Ctrl+P` | Print… |
| `Ctrl+Shift+T` | Toggle typewriter scroll (default on) |
| `Ctrl+Shift+F` | Toggle focus mode |
| `Ctrl+Shift+K` | Toggle typewriter key sounds (default off) |
| `Ctrl+Shift+I` | Insert table… |
| `Tab` / `Shift+Tab` | Next / previous table cell |

## License

MIT — Copyright (c) 2026 Stephen B. Johnson. See [LICENSE](LICENSE).

## Icon

Classic **typewriter** silhouette (`assets/icons/zwriter.svg` /
`zwriter-128.png`) — not a Word-like W, not a fountain pen.
