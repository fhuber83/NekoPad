# CLAUDE.md — working notes for AI assistants

Neko Pad is a C++23 / Qt 6 Widgets text and source code editor in the spirit of
Notepad++, for Windows, Linux and macOS. It is part of the "Neko" family with Neko Grid
and Neko Note; the editor control and the look come from NekoControls
(https://github.com/fhuber83/NekoControls). **Speed is the point**: measure before and
after anything on the load path.

## Build, run, test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/bench_load 10     # MiB; what opening a big file costs
./build/nekopad
```

Always run the tests after changing anything. `NEKOPAD_TEST_SCREENSHOTS=<dir>
./build/test_ui testScreenshots` saves light and dark screenshots to look at. To try the
app without touching the real settings or meeting a running instance:
`HOME=/tmp/np XDG_CONFIG_HOME=/tmp/np/.config ./build/nekopad --new-instance`.

## Naming

| Thing | Spelling |
|---|---|
| C++ namespace | `neko::pad` (core), `neko::pad::ui`, `neko::pad::lines` |
| Targets | `nekopad_core`, `nekopad_ui`, `nekopad`, `test_core`, `test_ui`, `bench_load` |
| CMake options / macros | `NEKOPAD_*` |
| Product name | "Neko Pad"; `QSettings` organization "Neko", application "Neko Pad" |

## Rules

- **`src/core` is Qt Core only and must not include `ui/`.** `test_core` links
  `nekopad_core` alone, so a violation fails to link there.
- **Only `neko::editor` and `neko::theme` from NekoControls**, added library by library
  (`SOURCE_SUBDIR` trick in `CMakeLists.txt`), never NekoControls' top level: that pulls
  in Qt WebEngine, which would cost start-up time and a heavy build dependency.
- **Nothing slow on the UI thread.** Files are read and decoded on a worker
  (`EditorTab::load`, `QThreadPool`). A large text's `QTextDocument` is built *and
  highlighted* on that worker too, with the editor's font and text options (so adopting
  it costs one layout, not three), and the UI thread only adopts it
  (`CodeEditor::replaceDocument`, NekoControls); a preview of its start shows meanwhile.
  Session tabs read their file when first shown. Highlighters are the document's
  children, so they move and die with it. The
  highlighter is a hand-written scanner (`ui/syntax_highlighter`): no
  `QRegularExpression` per line, no allocation per token, keyword lookup by bisection
  (`containsWord`).
- **Never lose bytes.** Save through `saveTextFile` (`QSaveFile`); take the text with
  `documentText()` (raw text), not `toPlainText()`, which turns non-breaking spaces into
  spaces. Keep the file's encoding and line endings unless the user converts.
- **Editing behaviour belongs in NekoControls**, not here: `CodeEditor`, `EditorFindBar`,
  `MarkdownHighlighter`. To change NekoControls alongside, configure with
  `-DFETCHCONTENT_SOURCE_DIR_NEKO_CONTROLS=/abs/path/to/NekoControls`; release it there
  (`scripts/make-release.sh`), then bump the URL and SHA-256 in `CMakeLists.txt`.
- **The look is NekoControls' `neko::theme`**: `ThemeManager` set up in `src/main.cpp`,
  View → Theme / Widget Style, `ResponsiveToolBar`.
- **UI icons: `neko::theme::themedIcon("<name>")`**, never an emoji. Names are the
  left-hand sides of `resources/icons.txt`; a new icon is a new line there (re-run
  CMake). `test_ui` fails on any name the map lacks.
- **A new language is an entry in `buildLanguages()`** (`src/core/language.cpp`); a new
  highlighting *family* is a scanner in `SyntaxHighlighter`. Add a test for either.
- **Menu shortcuts must not collide with `CodeEditor`'s own** (Alt+Z, Alt+Shift+arrows)
  or with `EditorFindBar::installShortcuts` — which is why the window drives the Find
  bar through its Search menu and does not install the bar's shortcuts.
- **New source files go into the explicit lists in `CMakeLists.txt`** (no globbing).

## Conventions

- `clang-format` 23 with the repository's `.clang-format`; format what you touch.
- `.hpp` / `.cpp`, `#pragma once`, `snake_case` files, `PascalCase` types, `camelCase`
  methods, trailing-underscore members, `[[nodiscard]]` on getters.
- `QStringLiteral` for literals, pointer-to-member `connect`.
- Warnings on (`-Wall -Wextra -Wpedantic`, MSVC `/W4`); keep the build clean.
- Conventional commits: `feat(scope):`, `fix(scope):`, `chore:`.

## Known limits and next steps

- **What is left on the UI thread for a big file** is one pass of Qt's
  `QPlainTextDocumentLayout` over every line when the editor adopts the document
  (~65 ms for 10 MiB, ~380 ms for 50 MiB / 1.7 M lines). Avoiding it would take a
  layout of our own.
- `QSyntaxHighlighter` keeps formats for every block; files over
  `EditorTab::kHighlightLimit` (32 MiB) are shown plain.
- Not built yet: Find in Files, bracket matching (needs an extra-selection hook in
  `CodeEditor`), split view, macros, code folding, auto-completion, backup of unsaved
  new documents across restarts, translations (NekoControls brings its own),
  packaging (AppImage, Windows installer, macOS bundle signing).
