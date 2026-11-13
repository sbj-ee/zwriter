# zwriter Help

This guide covers how zwriter works. Open it any time from **Help → zwriter Help**, or press `F1`. If the guide is already open, that command brings its window to the front.

- [Documents & Modes](#documents-modes)
- [Hide-Away Toolbar & View Modes](#hide-away-toolbar-view-modes)
- [Books & Table of Contents](#books-table-of-contents)
- [Outline & Styles](#outline-styles)
- [Export & Printing](#export-printing)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Updates](#updates)

On macOS, read **Ctrl** as **Cmd** unless a shortcut says otherwise.

## Documents & Modes

zwriter opens and saves **ODT** (the native rich-text format), **Markdown** (`.md` / `.markdown`), **plain text** (`.txt`), and **RTF** (best-effort). There is no separate zwriter file format.

**ODT (rich text).** **File → New** (`Ctrl+N`) starts an unsaved ODT document. You can apply character and paragraph formatting, tables, pictures, page breaks, headers and footers, and Full Page view. Formatting is kept in the file.

**Markdown.** **File → New Markdown Document** (`Ctrl+Alt+N`) starts a plain-text Markdown document. Markup such as `**bold**` and `# Heading` stays visible in the editor; zwriter styles it in place but does not turn it into rich text. The default view is an iA Writer Duo column (about 66 characters, 150 % line spacing) in a continuous strip — Full Page view is not used. The **formatting toolbar is hidden** in Markdown mode, and so is the **Format** menu, because those commands do not apply to plain-text Markdown. Pictures go in a `<name>.assets/` folder next to the saved file, as Markdown image links.

**Markdown preview.** **View → Markdown Preview** (`Ctrl+Alt+P`) shows a rendered preview beside the editor. It is available only while a Markdown document is open; the choice is remembered.

**Plain text (`.txt`).** A `.txt` file is stored as text only. zwriter opens it in the same editor as an ODT document (the formatting toolbar is shown), but saving keeps the text and drops formatting. `.txt` chapters in a book have no headings for the outline or the table of contents.

**RTF** can be opened and saved with common formatting, as a best-effort interchange format.

**File → New from Outline…** opens a box where you type an outline (one heading per line) and choose **Document (ODT)** or **Markdown**. *Create* starts a new unsaved document with those headings.

Use **File → Open…** (`Ctrl+O`) or **File → Open Recent** (`Ctrl+1` … `Ctrl+8`) to reopen a file. **File → Save** / **Save As…** write the current document; the format follows the name you choose.

## Hide-Away Toolbar & View Modes

**View → Always Show Toolbar** (or **Esc**) pins or unpins the menu bar, formatting toolbar and status bar. The choice is **remembered separately for ODT documents and Markdown documents**, so switching modes restores each one's last setting.

When the bars are not pinned (hide-away):

- Move the pointer to the **top or bottom 10 pixels** of the window, or over the menu bar, toolbar or status bar, and the bars reappear.
- They hide again about a second after the pointer leaves, unless a menu is open.
- On Linux and Windows, **Alt+letter** (for example Alt+F for File) reveals the bars and opens that menu. This is not used on macOS, where Option+letter types accented characters.

In **Markdown mode the formatting toolbar stays hidden** even when the other bars are shown.

**View → Focus Mode** (`Ctrl+Shift+F`) dims everything except the current paragraph or sentence. **View → Focus Scope** chooses **Paragraph** or **Sentence**.

**View → Typewriter Scroll** (`Ctrl+Shift+T`) keeps the caret vertically centred while you type or move through the document (on by default).

**View → Full Screen** (`F11`) fills the screen; the same command, or leaving full screen, restores the previous window. The choice is remembered for the next session.

Other views (rich documents):

- **View → Full Page** (`Ctrl+Shift+P`) — the default for ODT: a centred paper page. Markdown is always a continuous strip, so this is disabled there.
- **View → Fit Page to Width** (`Ctrl+Shift+W`) — in Full Page view, zoom the page to the window width.
- **View → Zoom In / Zoom Out / Actual Size** (`Ctrl++` / `Ctrl+-` / `Ctrl+0`, or Ctrl+scroll) — display zoom only (50 %–300 %); it does not change the document, print or export.
- **View → Ruler**, **Page Guides** (`Ctrl+Alt+G`), **Theme** (Paper, Dark room, Inverse) and the typewriter, spelling and typography toggles are also in the View menu.

## Books & Table of Contents

A **book** is a folder with a `book.json` that lists its chapters in order. Chapters may be **ODT, Markdown or plain text** (`.odt`, `.md`, `.txt`).

- **File → New Book…** or **View → Book Sidebar** (`Ctrl+Alt+B`) opens the Book sidebar; New Book creates the folder and `book.json`.
- **File → New Chapter…** (also the sidebar **+** menu or right-click) adds a chapter. You choose **ODT document (.odt)**, **Markdown (.md)** or **Plain text (.txt)**. Existing files in the book folder can be added too.
- In the sidebar, drag chapters to reorder them, press **F2** to rename, **Alt+Up** / **Alt+Down** to move a chapter, or Delete to remove it from the book (with confirmation). The Library sidebar (**View → Library Sidebar**, `Ctrl+Shift+L`) is a separate file list for a folder of Markdown and text files.

**File → Insert/Update Table of Contents** (also in the Book sidebar menu) is enabled only when a **book is open** and the **active document is not Markdown**. It writes the table into the book's Contents chapter:

- If there is no Contents chapter, zwriter creates **`00 Contents.odt`** as the first chapter (or `00 Contents 2.odt`, and so on, if that name is already used) and opens it.
- The table lists every other chapter in book order, with chapter numbers starting at 1 (the Contents chapter itself is not numbered), each chapter's headings, and — when they are known — the page numbers of the whole-book PDF, on a right tab with dot leaders.
- Running the command again updates the existing table. The table also refreshes when chapters are reordered, added or renamed, as long as the Contents document is an ODT file.

zwriter does not insert a table of contents into a standalone document, and it does not expand placeholders such as `{{ TOC }}` or `[TOC]` in Markdown files.

## Outline & Styles

**View → Outline** (`Ctrl+Alt+O`) shows the open document's headings in a pane to the right of the page (between the page and the Markdown preview, if both are on). The choice is remembered and is off by default. It works for ODT and Markdown alike:

- ODT / RTF: Heading 1–3. Markdown: `#` lines, levels 1–6 (fenced code is skipped). A heading with no text, and the generated table of contents, are not listed. A `.txt` file has no outline headings.
- Click an entry, or press Enter on it, to jump to that heading. Arrow keys only move through the list. **Esc** returns focus to the page. Opening the pane from the menu focuses the list.
- The highlighted entry follows the caret. The footer shows the heading count and the word count of the current section.

You can reorder and change level from the outline or the Edit menu (ODT and Markdown; each is one undo step):

- **Edit → Move Section Up / Down** (`Alt+Shift+Up` / `Alt+Shift+Down`) — also by dragging an entry onto a sibling, or from the outline's right-click menu.
- **Edit → Promote / Demote Section** (`Alt+Shift+Left` / `Alt+Shift+Right`) — also in the right-click menu as **Promote** / **Demote**. A change that would leave the allowed range (Heading 1–3 in rich documents, `#`–`######` in Markdown) is refused, with a note in the status bar.

**Format → Paragraph Style** applies **Body Text**, **Heading 1–3** (`Ctrl+Alt+0` … `Ctrl+Alt+3`) and the named styles Title, Subtitle, Block Quote, Caption and Bibliography. **Format → Styles…** sets a font family and size for each of those styles, with a live preview, **Reset to Defaults**, and **Save as default for new documents**. A document can keep its own style fonts (in the ODT). The Format menu and these commands are available in rich-text documents only.

**File → New from Outline…** (see [Documents & Modes](#documents-modes)) starts a document from a typed outline.

## Export & Printing

**The open document only**

- **File → Export as PDF…** (`Ctrl+Shift+E`) writes a PDF of the current document.
- **File → Print…** (the standard Print shortcut) and **File → Print Preview…** print that document.
- **File → Page Setup…** and **File → Page Size & Ruler Units…** set paper, orientation, margins and ruler units. New ODT documents start on US Letter; the size is stored in the `.odt`. Files that do not store a size (older files, Markdown, text) open on A4.

**The whole book** — **File → Export Book → PDF…** or **EPUB…**, and **File → Print Book…** (also in the Book sidebar). Every chapter is included in book order. A dialog offers a title page, a table of contents, and figure numbers.

- **PDF / Print Book.** Chapters are joined into one document, each starting on a new page. The table of contents (when that option is on) has real page numbers and dot leaders. If the book has a Contents chapter, the table is placed there, in that chapter's position in the book, instead of a second table after the title page. If the table-of-contents option is off, the Contents chapter is left out. Pages are numbered continuously through the book (not on the title page). The table in the editor uses these PDF page numbers when they are known.
- **EPUB.** Each chapter becomes an XHTML file. The book's Contents chapter is not included as a chapter; the EPUB table of contents is **`nav.xhtml`** (EPUB 3 navigation), listing each chapter and its headings. When the table-of-contents option is on, `nav.xhtml` is also placed in the reading order.

## Keyboard Shortcuts

These match the commands in zwriter's menus. On macOS use **Cmd** in place of **Ctrl**, except as noted.

### File

| Command | Shortcut |
| --- | --- |
| New | Ctrl+N |
| New Markdown Document | Ctrl+Alt+N |
| Open… | Ctrl+O |
| Open Recent (first eight files) | Ctrl+1 … Ctrl+8 |
| Save | Ctrl+S |
| Save As… | Ctrl+Shift+S |
| Export as PDF… | Ctrl+Shift+E |
| Print… | Ctrl+P |
| Quit | Ctrl+Q |

Insert/Update Table of Contents, New Book, New Chapter, Export Book, Print Book, Print Preview, Page Setup, Page Size & Ruler Units and Properties have no shortcut.

### Edit

| Command | Shortcut |
| --- | --- |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z |
| Cut / Copy / Paste | Ctrl+X / Ctrl+C / Ctrl+V |
| Paste as Plain Text | Ctrl+Shift+V |
| Select All | Ctrl+A |
| Find… | Ctrl+F |
| Find Next / Find Previous | the standard shortcut on your system (often F3 / Shift+F3) |
| Replace… | Ctrl+H (macOS: Cmd+Option+F) |
| Move Section Up / Down | Alt+Shift+Up / Alt+Shift+Down |
| Promote / Demote Section | Alt+Shift+Left / Alt+Shift+Right |

When the Book sidebar is visible, **Ctrl+Tab** and **Ctrl+Shift+Tab** move focus between the editor and the chapter list.

### Format and Insert

These formatting shortcuts apply in rich-text documents (the Format menu is hidden in Markdown mode).

| Command | Shortcut |
| --- | --- |
| Bold / Italic / Underline | Ctrl+B / Ctrl+I / Ctrl+U |
| Subscript / Superscript | Ctrl+, / Ctrl+. |
| Body Text, Heading 1–3 | Ctrl+Alt+0 … Ctrl+Alt+3 |
| Increase / Decrease Indent | Ctrl+M / Ctrl+Shift+M |
| Align Left / Center / Right / Justify | Ctrl+L / Ctrl+E / Ctrl+R / Ctrl+J |
| Bulleted / Numbered List | Ctrl+Shift+B / Ctrl+Shift+N |
| Clear Formatting | Ctrl+\\ |
| Insert Table… | Ctrl+Shift+I |
| Insert Page Break | Ctrl+Enter |
| Insert → Symbol… | Ctrl+Shift+Y |

### View and Help

| Command | Shortcut |
| --- | --- |
| Full Page | Ctrl+Shift+P |
| Fit Page to Width | Ctrl+Shift+W |
| Zoom In / Zoom Out / Actual Size | Ctrl++ / Ctrl+- / Ctrl+0 (also Ctrl+scroll) |
| Page Guides | Ctrl+Alt+G |
| Typewriter Scroll | Ctrl+Shift+T |
| Focus Mode | Ctrl+Shift+F |
| Typewriter Key Sounds | Ctrl+Shift+K |
| Always Show Toolbar | Esc |
| Markdown Preview | Ctrl+Alt+P |
| Outline | Ctrl+Alt+O |
| Library Sidebar | Ctrl+Shift+L |
| Book Sidebar | Ctrl+Alt+B |
| Full Screen | F11 |
| zwriter Help | F1 |

In the Book sidebar: **Alt+Up** / **Alt+Down** move the selected chapter, **F2** renames it. In the Outline pane, **Enter** jumps to the heading and **Esc** returns to the page. **Esc** also closes the Find / Replace bar when it is open, before it toggles the toolbar.

## Updates

**Help → Check for Updates…** asks GitHub for the latest zwriter release and reports the result: you already have the latest version, a check failed, or a dialog with the release notes and an **Install** button (the package is downloaded, its signed checksums are verified, then it is installed).

**Help → Check for Updates at Startup** (on by default) also runs a quiet check a few seconds after launch, at most once a day. Turn it off if you prefer to check only by hand.

**Help → About zwriter** shows the version you are running.
