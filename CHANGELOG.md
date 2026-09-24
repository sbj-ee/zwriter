# Changelog

All notable changes to zwriter. Versions follow [Semantic Versioning](https://semver.org/);
the version lives only in `project(zwriter VERSION …)` in `CMakeLists.txt`.

## [1.0.0] — 2026-09-24

First stable release. Linux amd64 and Apple Silicon only.

### Features
- Distraction-free Qt 6 Widgets writer: hide-away chrome (Esc to pin/unpin, F11 full screen),
  a quiet toolbar (font, size, B/I/U, paragraph style) and five menus: File, Edit, Format, View, Help.
- Files: ODT is the native default save format; TXT and basic RTF open/save; PDF export only.
  Open files from the command line or a file manager (Open With / double-click).
- Full Page view (default on): multi-page A4 at true physical size, stacked pages, Page N of M
  in the status bar; print and PDF match the screen. Default body font is Courier-class
  monospace at 12 pt.
- Themes: Paper and ink (paper-white, default) and Dark room.
- Headers and footers with `{page}` / `{pages}` tokens; page numbers are opt-in
  (Format → Page Numbers, off by default).
- Manual page breaks (Ctrl+Enter), tables (insert, merged cells from ODT, add/remove rows and
  columns), headings 1–3, alignment, bulleted/numbered/nested lists.
- Hunspell en_US spell check (default on) with suggestions, ignore and user dictionary.
- Typewriter scrolling, focus mode (paragraph or sentence), find/replace, recent files,
  smart quotes and dashes, word/character count and reading time.
- Optional typewriter key sounds (default off).
- Print, print preview and page setup; document properties (author, created, last edit) in ODT
  `meta.xml`.
- Help → About and Help → Check for Updates (GitHub latest release).
- CI builds and packages `zwriter-1.0.0-Linux-amd64.deb` and `zwriter-1.0.0-Darwin.dmg`,
  and runs the ODT round-trip test on both platforms.

### Known limitations
- ODT round-trips text (including repeated spaces, tabs, line breaks), fonts/sizes, bold/italic/
  underline, headings 1–3, alignment, lists, tables incl. merged cells, blank paragraphs, page
  breaks, header/footer and properties. It drops colours, images, footnotes, links, custom
  paragraph spacing/indents and per-table styling (custom table borders are lost). Heading and
  metadata saving need the `zip`/`unzip` tools.
- Typing slows down on very long documents (roughly 12 ms per keystroke at ~370 pages, ~48 ms at
  ~1,500 pages), because Qt re-lays out the rest of the document on each edit.
- The macOS `.dmg` contains a bare arm64 `zwriter` executable, not a `.app` bundle. It links
  Homebrew Qt and Hunspell under `/opt/homebrew` (`brew install qt hunspell` is required), and it
  is unsigned and not notarized, so Gatekeeper blocks the first launch (Control-click → Open, or
  `xattr -d com.apple.quarantine zwriter`).
