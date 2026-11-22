# Neko Pad

<img src="resources/icons/app/nekopad_256.png" alt="Neko Pad" width="128" align="right">

A fast, cross-platform text and source code editor in the spirit of Notepad++, in C++23
and Qt 6 Widgets, for Windows, Linux and macOS. The editor control and the look come from
[NekoControls](https://github.com/fhuber83/NekoControls), as in Neko Grid and Neko Note.

- **Fast.** No Qt WebEngine and no Chromium. Files are read, encoding-detected and decoded
  on a worker thread, and for a large file the document itself is built and highlighted
  there too: the window shows the file's start at once and is busy for ~70 ms of a
  10 MiB file's load instead of the whole second. The highlighter is a hand-written
  scanner without regular expressions. A restored session reads only the tab you look
  at. Opening a file while Neko Pad runs hands it to the running window and quits in
  about 20 ms. Release builds use LTO and `-march=x86-64-v2`.
- **Tabs** like Notepad++: new / open / save / save as / save all, close others / to the
  right, reopen closed tab (Ctrl+Shift+T), recent files, drag & drop, middle-click
  closes, double-click on the empty tab bar opens a new document. Files changed by other
  programs are offered for reloading.
- **Encodings**: UTF-8, UTF-8 BOM, UTF-16 LE / BE BOM and Latin-1, detected on open and
  written back as found. Convert, or reload in another encoding. **Line endings**:
  CR LF / LF / CR detected (mixed endings shown) and converted.
- **29 languages** recognized by name, extension or `#!` line, with highlighting that
  follows light and dark themes: C, C++, C#, Java, Kotlin, Swift, Go, Rust, JavaScript,
  TypeScript, JSON, Python, Ruby, PHP, Perl, Lua, Shell, PowerShell, Batch, SQL, CMake,
  Makefile, YAML, INI / TOML, CSS, XML, HTML, Markdown, Diff.
- **The editor** (NekoControls' `CodeEditor`): line numbers, current line, column
  selection (Alt + drag, Alt+Shift+arrows), Find / Replace with regular expressions,
  word wrap (Alt+Z per document).
- **Line operations**: duplicate, delete, move up / down, join, sort, remove duplicate or
  empty lines, trim, tabs ↔ spaces, UPPER / lower case, toggle comment in the
  document's language.
- **Status bar**: line, column (tabs counted), selection, length, line endings,
  encoding, language (the last three are menus), INS / OVR.
- **The Neko look**: ten themes and the platform's widget styles (View → Theme / Widget
  Style), Lucide icons that follow the theme.

## Build and run

Requirements: CMake 3.24, a C++23 compiler (GCC 13+, Clang 17+, MSVC 2022 17.10+), Qt 6.5+
with Widgets, Svg and Network. Configuring downloads NekoControls and the Lucide icons
(pinned, SHA-256 checked). Qt WebEngine is *not* needed.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
./build/nekopad [files...]          # file:line jumps to a line
```

Options: `--new-instance` opens a second window instead of handing files to the running
one; `--no-session` skips the last session's files. `NEKOPAD_CPU_ARCH` picks the target
CPU (`x86-64-v2` by default, `x86-64-v3`, `x86-64-v4`, `native`, `none`); `NEKOPAD_LTO`
turns link-time optimization off.

To see what opening a large file costs, step by step:
`QT_QPA_PLATFORM=offscreen ./build/bench_load 50` (MiB).

## Shortcuts

| Shortcut | | Shortcut | |
|---|---|---|---|
| Ctrl+N / Ctrl+O | New / Open | Ctrl+F / Ctrl+H | Find / Replace |
| Ctrl+S | Save | F3 / Shift+F3 | Find next / previous |
| Ctrl+Alt+S | Save As | Ctrl+G | Go to line |
| Ctrl+Shift+S | Save All | Ctrl+D | Duplicate line (or selection) |
| Ctrl+W / Ctrl+Shift+W | Close / Close all | Ctrl+Shift+L | Delete line |
| Ctrl+Shift+T | Reopen closed tab | Ctrl+Shift+Up/Down | Move line |
| Ctrl+Tab, Ctrl+PgDn | Next tab | Ctrl+J | Join lines |
| Ctrl+Shift+Tab, Ctrl+PgUp | Previous tab | Ctrl+/ (Windows: also Ctrl+Q) | Toggle comment |
| Ctrl++ / Ctrl+- / Ctrl+0 | Zoom | Ctrl+U / Ctrl+Shift+U | lower / UPPER case |
| F11 | Full screen | Alt+Z | Word wrap (this document) |

## Layout

| Path | What |
|---|---|
| `src/core/text_encoding` | Encoding detection (UTF-8 validation eight bytes at a time), decoding, line endings |
| `src/core/text_file` | Loading (memory-mapped) and atomic saving (`QSaveFile`) |
| `src/core/language` | The language table: recognition and what the highlighter needs |
| `src/core/line_ops` | The Edit menu's line operations, as pure functions |
| `src/ui/syntax_highlighter` | One scanner per language family: code, markup, INI, diff |
| `src/ui/editor_tab` | A document: `CodeEditor` + `EditorFindBar`, loading and building on a worker |
| `src/ui/main_window` | Tabs, menus, tool bar, status bar, session, files changed on disk |
| `src/ui/single_instance` | Hands files to the running window over a local socket |
| `src/ui/settings` | The `QSettings` keys |
| `tests/` | `test_core` (links the core alone), `test_ui` (the window, offscreen), `bench_load` |

## License

MIT — see `LICENSE`. The Lucide icons (ISC) are compiled in through NekoControls; their
notice is in NekoControls' `THIRD_PARTY_NOTICES.txt`.
