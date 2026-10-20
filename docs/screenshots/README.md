# Screenshots

Captured from the real MainWindow via `./build/zwriter --capture-screenshots docs/screenshots` (headless: `QT_QPA_PLATFORM=offscreen`, or xvfb-run). Page numbers are off by default; shots that show them turn them on explicitly.

| File | Shows |
|---|---|
| `editor-chrome.png` | File / Edit / Format / View / Help menus, formatting toolbar, status bar (words · chars · ~min) |
| `editor-focus.png` | Focus mode — current paragraph bright, others dimmed |
| `find-bar.png` | Find bar (Ctrl+F) with match highlight + focus mode |
| `about.png` | Help → About zwriter (version from PROJECT_VERSION) |
| `editor-table.png` | Inserted QTextTable with cell editing (ODT/PDF-safe) |
| `save-document.png` | Polished Save Document dialog — ODT default, sidebar, **New Folder**, type filter |
| `fonts-toolbar.png` | Paper-white page + font family/size controls; sample text in typewriter + other faces |
| `full-page.png` | Full Page view — A4 · 12 pt density · Lorem body · header + opt-in footer page number (page guides on) |
| `full-page-multipage.png` | Full Page view, two true-size A4 sheets stacked on the desk; Chapter Two starts on page 2 after a manual page break (Ctrl+Enter); header + `Page {page} of {pages}` footer. Window made taller (1024×2440), page size not scaled |
| `spell-check.png` | Spell check on — red underlines on intentional misspellings (Hunspell en_US) |
| `markdown-mode.png` | Markdown mode (`.md`) — inline styling with greyed markers, iA Writer Duo, ~66-character column, chrome hidden |
| `markdown-chrome.png` | Markdown mode with chrome revealed — no formatting toolbar, Format menu hidden, slim status bar |
| `markdown-focus-dark.png` | Markdown mode, Dark room theme, focus mode on sentence scope |
| `markdown-preview.png` | Markdown mode with View → Markdown Preview (`Ctrl+Alt+P`) — editor and rendered preview side by side (QTextDocument::setMarkdown), Paper theme |
| `markdown-preview-dark.png` | The same in the Dark room theme — the preview follows the theme (page colours, code / link / quote inks) |
| `library-sidebar.png` | View → Library Sidebar (`Ctrl+Shift+L`) — a demo library folder; only folders and `.md` / `.txt` files are listed (its `.odt`, `.png` and `.docx` files are hidden); the open document is highlighted |
| `library-and-preview.png` | Library sidebar, editor and preview together |
| `book-sidebar.png` | View → Book Sidebar (`Ctrl+Alt+B`, 1.4.0) — a demo library with two books (one `book.json` per subfolder), "The Harbor" shown, chapters in book order with status · words / target · POV, a missing chapter in italics, chapter totals in the footer |
| `book-context-menu.png` | Right-click a chapter — Open, Edit Details…, Move Up / Move Down, Remove from Book, Add Chapters… (the popup is painted onto the window grab) |
| `book-picker.png` | The book picker open — one entry per book subfolder, by title (the popup is painted onto the window grab) |
| `book-new.png` | New Book — pick a library subfolder without a book, or type a new name |
| `book-chapter-details.png` | Chapter Details — title, status (Idea / Outline / Draft / Revised / Final), POV, tags, word target with the current count, synopsis, notes |
| `book-sidebar-dark.png` | The Book sidebar in the Dark room theme |
