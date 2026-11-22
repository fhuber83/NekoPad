#pragma once

#include <QString>
#include <QStringList>

namespace neko::pad::lines {

// Operations on whole lines, for the Edit menu: the UI takes the lines the selection
// touches, runs one of these and puts the result back as one undoable edit. Pure.

[[nodiscard]] QStringList sorted(QStringList lines, bool ascending, Qt::CaseSensitivity cs);
/// Lines in order, every repeat of an earlier line removed.
[[nodiscard]] QStringList withoutDuplicates(const QStringList& lines);
[[nodiscard]] QStringList withoutEmpty(const QStringList& lines, bool blankCountsAsEmpty);
[[nodiscard]] QStringList trimmedTrailing(QStringList lines);
[[nodiscard]] QStringList trimmedLeading(QStringList lines);

/// Line comments on or off: if every non-blank line starts with `token` (after its
/// indentation), removes it with one space after it; otherwise adds `token` and a space
/// at the smallest indentation of the non-blank lines. Blank lines stay as they are.
[[nodiscard]] QStringList toggledLineComment(QStringList lines, const QString& token);

/// The same with a block comment around each line (`<!-- ... -->`, `/* ... */`), for
/// languages without line comments.
[[nodiscard]] QStringList toggledBlockComment(QStringList lines, const QString& start, const QString& end);

/// Leading indentation as spaces / as tabs (`tabWidth` columns per tab).
[[nodiscard]] QStringList indentationToSpaces(QStringList lines, int tabWidth);
[[nodiscard]] QStringList indentationToTabs(QStringList lines, int tabWidth);

/// All lines as one, joined by a single space, each trimmed.
[[nodiscard]] QString joined(const QStringList& lines);

/// The display column of `positionInLine` in `line`, tabs expanded to `tabWidth`; zero
/// based.
[[nodiscard]] int displayColumn(QStringView line, int positionInLine, int tabWidth);

} // namespace neko::pad::lines
