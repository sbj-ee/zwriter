# Changelog

All notable changes to zwriter. Versions follow [Semantic Versioning](https://semver.org/);
the version lives only in `project(zwriter VERSION …)` in `CMakeLists.txt`.

## [1.5.1] — unreleased

Images in documents, part 2 of 3: insert pictures, size them and wrap text around them.

- **Format → Insert Image…** picks a PNG, JPEG, GIF (first frame), WebP or BMP file. SVG works
  when Qt's SVG image plugin is installed; otherwise it's refused with a message. The picture
  goes in line at the caret, at its own size but no wider than the text column.
- **Paste and drag-and-drop.** Pasting image data (a screenshot, Copy Image in a browser) or
  pasting or dropping image files inserts the pictures. A multi-file drop is one undo step.
  Image data that arrives with text (some apps add a bitmap of copied text) still pastes as text.
  Markdown documents don't take pictures (Markdown gets them in part 3).
- **Big pictures are scaled down** to 4000 px on the longest side, with a status-bar note. PNG
  and JPEG keep their bytes; other types are stored as PNG, which is what ODT holds anyway.
- **Right-click a picture → Image Properties…**: width and height in px (Keep proportions on by
  default), **Fit Column Width**, **Original Size** (capped to the column), and **Wrap**: in line
  with text, left (text flows on the right) or right (text flows on the left). One undo step.
- **Wrap saves to ODT.** A floating picture is saved as a char-anchored frame with a graphic style
  (`style:wrap`, `style:horizontal-pos` left/right), which LibreOffice shows the same way.
  Opening an ODT reads those styles back, including LibreOffice's paragraph-anchored frames
  with wrap and a left/right position. Centred frames show in line.
- Tests: `images_test` W (left/inline/right survive an ODT save/open, graphic styles, anchors),
  N (column cap, 4000 px limit, JPEG bytes kept, GIF to PNG, refusals, undo of insert and of
  Image Properties), L (LibreOffice right-floated frame). `mainwindow_test` P (Insert Image,
  paste, drop and its undo, click hit-test, Markdown refuses, dialog maths). Screenshots
  `images-wrap-*.png`, `images-properties.png`.

## [1.5.0] — unreleased

Images in documents, part 1 of 3: pictures in ODT files open, show and save.

- **ODT pictures open and show.** Opening an `.odt` now loads its pictures from the package
  (`Pictures/…`) with the size the file gives the frame (cm, mm, in, pt, pc or px). Before, they
  were dropped without a word. A frame holding several images, like LibreOffice's SVG with a
  PNG fallback, shows the first one this build can draw. A picture missing from the package is
  skipped and the text around it is kept.
- **ODT pictures save.** Saving keeps every picture in the package and the manifest, at its
  displayed size. JPEG photos are stored as the original JPEG bytes instead of Qt's PNG
  re-encode, so they don't grow several times over.
- **Saving as TXT, RTF or Markdown with pictures asks first.** Those formats can't hold images
  yet. The new warning offers **Save Without Images** or **Cancel** (the default). RTF and
  Markdown get image support later in this series.
- New `src/Images.*`: image resources named `zw-image-<n>.<type>` holding the original bytes, plus
  ODF length conversion.
- New `images_test`: unit conversion, the PNG + JPEG round trip (package members, manifest
  types, JPEG bytes, display size, pixels, a second save/open cycle), and a LibreOffice-shaped
  fixture (cm frame, SVG with PNG fallback, missing member). `mainwindow_test` I: an ODT with a
  picture opens clean and the picture is drawn. Screenshots `images-odt-*.png`.

## [1.3.2] — unreleased

- **Page guides frame the text again.** In the continuous view (Full Page off) the guides were
  drawn at a fixed tenth of the editor's viewport, while the text column starts at the
  viewport's own left edge (the column is centred by the viewport inset, plus any root-frame
  margins). So typed text began left of the left guide, outside the "page", by a tenth of the
  column width, in every theme. The guides are now drawn at the text column's real edges
  (root-frame margins + document margin, in document units, zoom-aware) and follow window
  resizes and the Library sidebar. Full Page view was already correct and is unchanged.
- Test: with Page Guides on, the caret at the start of a line sits on the left guide and the text
  stays inside the guides for the dark, paper and inverse themes, continuous and Full Page views,
  window widths 640–1920 px, with and without the Library sidebar.

## [1.3.1] — unreleased

- **zwriter's own release key.** The updater now trusts minisign key `DBC46B23C3AF0EF3` (zwriter's
  own, replacing the zterminal key `32960BBA77B36F1F` that 1.3.0 embedded), the key that
  `release.yml` signs with. 1.3.0 was never tagged or released. The real-signature test (a
  published zterminal `SHA256SUMS`) now pins zterminal's key as a constant in the test instead of
  reading the embedded key, so it still proves real-minisign interoperability; the embedded
  key's ID and bytes are pinned by their own test. Docs name the new key. No other behaviour
  change.

## [1.3.0] — unreleased

In-app updates, the same flow zterminal has: download, verify, install, offer a restart.

- Help → Check for Updates now downloads and installs the new release instead of only opening its
  page. It shows the release notes with Install / Later / Skip This Version. Linux: the `.deb` is
  installed with `pkexec apt install` (a system password prompt, no terminal). macOS: the verified
  `.dmg` is opened for a drag-to-Applications install.
- Nothing is installed unless the release's `SHA256SUMS` has a valid minisign signature
  (`SHA256SUMS.minisig`) from the release key built into the app and the package matches it; a
  release without the signature is refused with a message. The package is copied to a private folder
  and its SHA-256 re-checked right before it is handed to apt.
- A Restart prompt follows an install (it warns about unsaved changes, and the usual "Save changes?"
  question still comes first); the restart reopens the same document.
- A quiet check a few seconds after start, at most once a day. Help → Check for Updates at Startup
  turns it off (`updates/checkOnStartup`).
- Releases are now built, signed and published by `.github/workflows/release.yml` when a `vX.Y.Z` tag
  is pushed; `docs/RELEASING.md` has the one-time signing-key setup. Releases up to 1.2.1 were not
  signed, so install 1.3.0 by hand once; updates after that are verified in the app.
- New dependency: libsodium (Ed25519 and BLAKE2b for the signature check). The `.deb` depends on
  `libsodium23`; the `.dmg` bundles libsodium.
- Tests: `minisign_test` (fixtures from the real `minisign` tool, tampering and malformed files) and
  `update_test` (version compare, release JSON, checksum, daily limit and skip rules, the download and
  verify chain, and the whole check → install → restart flow against a mock GitHub on a local
  socket; no network, no apt).

## [1.2.1] — 2026-10-18

Fixes for the library sidebar and for a false "Save changes?" prompt, and the title bar now
says which build is running.

### Fixed
- **View → Library no longer looks like a hang.** With no library folder set it used to pop a
  GTK folder dialog straight away; under Wayland/GNOME that native dialog can open *behind* the
  window, and because it is modal the app looked frozen with no way to reach it. Now the sidebar
  opens first with a "Choose a library folder" hint and a **Choose Folder…** button. The chooser
  (that button, the "…" button, or View → Choose Library Folder…) is a Qt folder dialog parented
  to the main window, window-modal, raised and activated when it opens, and it no longer blocks
  the event loop. Picking a folder shows the sidebar and remembers the folder.
- **An untouched document no longer asks "Save changes?"** The prompt followed
  `QTextEdit::textChanged`, which `QSyntaxHighlighter::rehighlight()` also fires, and Qt marks
  the document modified while it re-styles. Anything that restyled the text (Spell Check on/off,
  adding a word to the dictionary, Markdown colours) therefore made an unedited document look
  edited. Unsaved changes now follow the document's modified flag, and highlighter restyling
  puts the flag back. Save also now clears the flag (it used to stay set after saving).
- The library sidebar was checked against a 220,000-file library with 16,000 folders and symlink
  loops: it lists lazily, never stalls the UI and sits at 0 % CPU when idle. No scan problem
  was found, so none was changed.

### Changed
- The title bar and About box show the version and commit: `notes.odt — zwriter 1.2.1 (abc1234)`.
  The commit is baked in at configure time (`-DZWRITER_GIT_COMMIT=…`, the `ZWRITER_GIT_COMMIT`
  environment variable, or `git rev-parse --short=7 HEAD`); builds outside a git checkout show
  the version alone. `zwriter --version` prints it too. The .deb and .dmg pick it up from the
  CMake build as before.

### Tests
- `library_test`: the empty-library hint and button, the chooser's parent/modality, pick/cancel
  behaviour, a big library with symlink loops (no event-loop stall, idle CPU), the version label.
- `mainwindow_test` (the real window, offscreen): every file type opens clean and closes without
  a prompt, also after resizes, theme/view toggles and Library; a real edit still prompts; Save
  clears the state; the title carries version and commit.

## [1.2.0] — 2026-10-01

iA Writer-style writing: a Markdown document mode with inline styling and the iA Writer fonts
(phase 1, #1), plus a live Markdown preview and a library sidebar (phase 2, #2).

### Markdown mode
- `.md` and `.markdown` files open in a new Markdown mode and are saved back byte for byte: line
  endings (LF or CRLF), a UTF-8 BOM, tabs, trailing spaces and a missing final newline are kept, and
  nothing else is written into the file. An edit changes only the text you typed. Files with mixed
  or lone-CR line endings or invalid UTF-8 still open, with a status-bar note that saving will
  normalise them. Before, `.md` files opened as plain text.
- File → New Markdown Document (`Ctrl+Alt+N`).
- Markdown is styled inline: headings, bold, italic, code spans, fenced code blocks, links, URLs,
  lists, task boxes, block quotes and rules. The markers stay in the text and are greyed out.
  Colours come from the theme. Spell check skips code, URLs and markup.
- The Format menu, formatting toolbar, font controls, tables, page breaks, page numbers,
  headers/footers and Full Page view are switched off in Markdown mode. Chrome is hidden by default
  (its pinned state is remembered separately from ODT mode) and the status bar is slimmer.
- Text is set in iA Writer Duo in a centred column of about 66 characters with ~150 % line height.
  None of this is saved into the file.
- The Linux desktop entry registers `text/markdown`; the macOS app declares `.md` / `.markdown`.
- Save As between Markdown and ODT/TXT/RTF writes the new file and reopens it in the matching mode.

### Markdown preview
- View → Markdown Preview (`Ctrl+Alt+P`, default off) shows the document rendered in a read-only
  pane beside the editor, using Qt's Markdown renderer (`QTextDocument::setMarkdown`, GitHub
  dialect). It is available in Markdown mode only; ODT, TXT and RTF documents never show it.
- The preview re-renders about 300 ms after the last edit, not on every keystroke, and a hidden
  preview does no work at all. It follows the editor's scroll position proportionally.
- It takes its colours from the theme (page colours, and the code, link and quote inks of the
  editor's inline styling) and is set in iA Writer Duo. Web and mail links open in the browser;
  relative images load from the document's folder.
- The choice is remembered (`markdown/previewVisible`).

### Library sidebar
- View → Library Sidebar (`Ctrl+Shift+L`, default off) lists the folders and the `.md` / `.txt`
  files under a folder you pick (View → Choose Library Folder…, or the `…` button). Other files and
  hidden entries are left out.
- Clicking a file opens it, after the usual unsaved-changes prompt; cancelling keeps the current
  document. The open document is highlighted in the tree.
- The folder and whether the sidebar is shown are remembered (`library/rootPath`,
  `library/visible`).

### Fonts
- iA Writer Duo and iA Writer Mono (static cuts, SIL OFL 1.1, by Information Architects, from
  github.com/iaolo/iA-Fonts) are bundled and embedded, so they appear in the font picker. The license
  is shipped with the packages and credited in About and the README. ODT documents keep the
  Courier-class default.

### Focus mode
- Sentence scope uses Unicode sentence boundaries (`QTextBoundaryFinder`), so `?`, `!`, closing
  quotes and non-Latin sentence punctuation end a sentence correctly.
- Each theme has its own dim colour for the out-of-focus text (Inverse is a little brighter than
  before, so dimmed text stays readable on black).

### Fixes
- Continuous view could lay text out with zero width (blank page) after switching from Full Page
  view until the window was resized.

### Internal
- Spell check and Markdown styling share one highlighter (`WritingHighlighter`, replacing
  `SpellHighlighter`).
- New `markdown_test`: byte-identical round trips, edit-then-save, undo-to-original, the Markdown
  scanner, highlighter, sentence boundaries, theme colours and font registration.
- Screenshots regenerated, including three Markdown-mode shots.
- New `PreviewPane` and `LibrarySidebar` widgets; `MainWindow` only places them in a splitter
  (library | page | preview) and wires the View actions. New `panes_test`: preview debounce,
  render, theme colours and scroll sync; library file filter and click-to-open; settings keys.
  Four new screenshots (`markdown-preview`, `markdown-preview-dark`, `library-sidebar`,
  `library-and-preview`); the existing ones are pixel-identical.

## [1.1.5] — 2026-09-26

Fit Page to Width: zoom the Full Page view to the window.

### Fit Page to Width
- View → Fit Page to Width (`Ctrl+Shift+W`, default off) zooms the Full Page view so the page fills
  the window width, and follows the window as it is resized. The document keeps its true-size
  layout and only painting and pointer positions are scaled, so lines and pages break exactly as
  they print, and turning it on or off is never an edit (no undo step, no unsaved-changes mark).
  The setting is remembered. It is unavailable in the continuous view, which already fills the
  window.

## [1.1.4] — 2026-09-26

### Typewriter key sounds
- Typing past the end of a line now plays the carriage-return sound, the same one Enter plays,
  when the text wraps onto the next line. The key click for the character plays first, then the
  return. Space, Backspace, Delete, arrow keys and pasting never trigger it.

## [1.1.3] — 2026-09-25

Paragraph alignment on the toolbar, and alignment round-trip fixes.

### Paragraph alignment
- Align Left, Center, Align Right and Justify are on the toolbar as well as in Format → Align
  (`Ctrl+L` / `Ctrl+E` / `Ctrl+R` / `Ctrl+J`, `Cmd` on macOS). The buttons are one exclusive
  group with drawn icons in each theme's ink (Paper, Dark room, Inverse); the checked one
  follows the paragraph at the cursor on every cursor or selection move, and after undo/redo.
- Alignment applies to the paragraph at the cursor or to every paragraph a selection touches
  (the selected cells of a table selection) as one undo step. Choosing the alignment a paragraph
  already has is not an edit: no undo step, and the document is not marked modified.
- Undo/redo of a formatting-only step (alignment, bold, …) leaves the caret where it was; Qt
  moved it to the end of the reformatted paragraph, into the next one.
- Enter in an empty centered, right-aligned or justified paragraph adds another paragraph with
  the same alignment. Qt reset the empty paragraph to left and swallowed the key, so a blank
  line under a centered title lost its centering.
- On KDE, whose standard Replace shortcut is `Ctrl+R`, `Ctrl+R` was bound twice (Replace and
  Align Right), so it did nothing. Align Right keeps `Ctrl+R`; Replace keeps `Ctrl+H`.
- ODT: justified paragraphs reopened as left-aligned (Qt's HTML import ignores
  `text-align:justify`). Alignment is now also read from LibreOffice files that set it through
  styles: `start` / `end` values, automatic styles inheriting from a parent, and named styles
  in `styles.xml` (e.g. a centered Title, a justified Text Body).
- RTF: import now reads `\ql` / `\qc` / `\qr` / `\qj` (and `\qd` as justified), reset by
  `\pard`; before, every imported paragraph was left-aligned. Export also writes `\ql`.
- Full Page view, Print, Print Preview and PDF export honour alignment on every page; the last
  line of a justified paragraph stays ragged. Checked by the new `alignment` ctest (ODT and RTF
  round-trips including LibreOffice/Word-style files, one-step undo, checked state, print layout).

### Command line
- `zwriter --version` (`-v`) prints the version and `--help` (`-h`) prints usage. Both answer
  immediately, without starting the GUI or needing a display; before, they launched the app.

## [1.1.2] — 2026-09-25

Fixes from the v1.0.0 GUI test report.

### Printing, PDF and page setup
- PDF export, Print and Print Preview drew body text about 12.5× too small (the layout is at
  screen DPI and was painted onto the 1200 dpi printer unscaled). The document is now laid out
  on a clone at screen DPI and the painter is scaled to the device, so text, header, footer and
  page numbers come out at their real size.
- Landscape in Page Setup now gives landscape sheets in Full Page view, with margins and header
  intact.
- Print Preview no longer shifts the page content by the margins (the footer fell off the sheet).

### ODT
- `meta.xml` is listed in `META-INF/manifest.xml`: LibreOffice refused to open zwriter files.
- Header, footer (with `{page}` / `{pages}` as ODF fields), paper size, orientation and margins
  are written as an ODF master page in `styles.xml`, so other applications show them.
- Tables are saved with 0.5 pt light-grey borders and a 100 % relative width (was 1.125 pt and a
  fixed 100 pt).
- Reopened documents keep their spacing (no extra space between paragraphs or list items) and
  the Courier fallback chain.
- With Qt 6.4 (the Debian/Ubuntu build) every saved text run was recorded as font "Sans", so
  documents reopened in a sans-serif face. The real font is now written.

### RTF
- Import no longer leaks the font, colour and style tables into the text; `\uN` and `\'hh`
  escapes decode properly; bold, italic, underline, larger (heading) sizes, page breaks and
  tables are kept.
- Export writes the document font, bold / italic / underline / strike, sizes, alignment, lists
  (as literal markers), tables and page breaks.

### Editing
- Tables draw as a single 1 px light-grey grid instead of double rules. Insert Table puts the
  caret in the first cell; Tab / Shift+Tab move between cells and Tab in the last cell adds a row.
  Removing a row or column keeps the caret in the table.
- The font and size boxes hand focus back to the editor only when a choice is made, so a typed
  font name or repeated size arrows no longer spill into the document.
- Typing works immediately at launch; a new window has nothing to undo.
- Switching Full Page view on or off is no longer an undoable edit.
- Paste as Plain Text works for HTML-only clipboards; rich pastes drop text and background
  colours so pasted text follows the theme.
- The spelling menu lists the best suggestion first.

### Find, menus and dialogs
- Find shows "Not found" (tinted field and status message) and leaves the caret where it was;
  wrapping around is reported. F3 / Shift+F3 search again with the bar closed.
- Replace is also on `Ctrl+H` explicitly (`Cmd+Option+F` on macOS).
- Unique menu mnemonics (Paste as P&lain Text, Insert Tabl&e…, Insert Page Brea&k, Spe&ll Check,
  Full Sc&reen).
- With the chrome hidden, hovering the top edge reveals it in Full Page view too, and Alt+letter
  reveals the chrome and opens that menu.
- Check boxes in the find bar and dialogs are visible in every theme.
- Save As for an untitled document defaults to ODT; a typed `.odt` / `.txt` / `.rtf` extension
  decides the format.
- The recent-files list drops files that no longer exist.

### Known issues
- Typing near the top of a long document still lags (about 13 ms per character at 90 pages,
  about 50 ms at 350): Qt re-lays out the whole paginated document on every edit.
- Changing margins in Page Setup still adds an undo step.
- RTF import keeps only sizes of 13 pt and up (as headings); lists are exported as literal text.

## [1.1.1] — 2026-09-24

### Linux
- The `.deb` installs `/usr/bin/zw` as a symlink to `zwriter`, so the app can be started from a
  terminal under either name. Both take the same arguments. Running as `zw` changes nothing else:
  the application, organisation and desktop-file names are set explicitly, so settings, window
  title and dock icon are unaffected.

## [1.1.0] — 2026-09-24

### All platforms
- New **Inverse** theme (View → Theme): a true black page with white text, for maximum
  contrast. The chrome follows the page like the other themes. The desk behind the sheet
  lifts one shade and the page border stays bright, so the page edge is still visible in
  Full Page view; selection is inverse video (white on black). Persisted in QSettings
  alongside Paper and Dark room.
- Header and footer ink is now always dark when printing or exporting to PDF. It previously
  came from the screen theme, so Dark room printed pale grey headers onto white stock.

### Linux
- The menu bar is no longer missing on desktops running a global menu. Where an appmenu
  service (`com.canonical.AppMenu.Registrar`, e.g. Fildem on GNOME) is present, Qt exported
  the menu bar over D-Bus, leaving no visible menu when the panel applet did not render it —
  and quietly breaking hide-away, which reveals chrome from the menu bar's geometry. zwriter
  now keeps the bar in the window. macOS keeps its native menu bar.

## [1.0.1] — 2026-09-24

### macOS
- The `.dmg` now contains a real **zwriter.app** (drag it to Applications) instead of a bare
  executable. Info.plist with bundle id `ee.sbj.zwriter`, version from `PROJECT_VERSION`,
  Retina support, macOS 14 minimum, and ODT / plain text / RTF document types; typewriter
  `.icns` icon generated from the existing art. Opens from Finder without a Terminal window.
- Self-contained: macdeployqt bundles the Qt frameworks and plugins; libhunspell and the en_US
  dictionary are inside the app (the spell checker looks in `Contents/Resources/hunspell`
  first); the key sounds are in `Contents/Resources/assets/sounds`. Homebrew is not needed.
- CI checks every Mach-O file in the app with `otool` and fails on any `/opt/homebrew` or
  `/usr/local` reference, and verifies the ad-hoc signature (`codesign --verify --deep --strict`).
- Still no Developer ID signature or notarization: first launch needs right-click → Open, or
  `xattr -dr com.apple.quarantine /Applications/zwriter.app`.

### All platforms
- Help → Check for Updates now says why a check failed (network error, HTTP error, GitHub rate
  limit with the reset time, unreadable reply, no published release) in the status bar and a
  message box, instead of failing silently.

### Linux
- No changes besides the version (`zwriter-1.0.1-Linux-amd64.deb`, Ubuntu 24.04+ / Debian 13+).

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
- (Fixed in 1.0.1) The macOS `.dmg` contains a bare arm64 `zwriter` executable, not a `.app` bundle. It links
  Homebrew Qt and Hunspell under `/opt/homebrew` (`brew install qt hunspell` is required), and it
  is unsigned and not notarized, so Gatekeeper blocks the first launch (Control-click → Open, or
  `xattr -d com.apple.quarantine zwriter`).
