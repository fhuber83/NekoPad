#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstdint>
#include <span>

namespace neko::pad {

/// How a language is highlighted (ui::SyntaxHighlighter): one scanner per family, the
/// details from the Language entry.
enum class LanguageFamily : std::uint8_t {
    Plain,    ///< no highlighting
    Code,     ///< keywords, strings, comments, numbers -- C, Python, SQL, shell, ...
    Markup,   ///< tags, attributes, entities, comments -- XML, HTML
    Markdown, ///< neko::editor::MarkdownHighlighter
    Diff,     ///< added / removed / hunk lines
    Ini,      ///< [sections], keys, values, comments -- INI, TOML, .properties
};

/// One entry of the language table: how a file is recognized and how it is coloured.
/// Word lists are sorted at start-up; for a case-insensitive language they are lower
/// case.
struct Language {
    QString id;   ///< stable, for settings: "cpp", "python", "text"
    QString name; ///< for menus: "C++", "Python", "Normal Text"
    LanguageFamily family = LanguageFamily::Plain;

    QStringList extensions; ///< lower case, without the dot
    QStringList fileNames;  ///< whole names: "Makefile", "CMakeLists.txt"
    QStringList shebangs;   ///< interpreters a `#!` line may name: "python3", "bash"

    QStringList keywords;
    QStringList types;
    QStringList constants; ///< true, false, null, ...

    QString lineComment;                    ///< "//", "#", "--"; empty if none
    QString blockStart;                     ///< "/*"; empty if none
    QString blockEnd;                       ///< "*/"
    QString quotes = QStringLiteral("\"'"); ///< characters that open a string
    bool tripleQuotes = false;              ///< Python's ''' and """ span lines
    bool backtickMultiline = false;         ///< `...` spans lines (JavaScript templates, Go raw)
    bool preprocessor = false;              ///< `#` at the start of a line is a directive (C, C#)
    bool caseInsensitive = false;           ///< SQL, batch: keywords match in any case
    bool variables = false;                 ///< `$name` / `${name}` / `%name%` are variables
    bool annotations = false;               ///< `@Name` (Java, Python decorators, C#)
    QString extraIdentChars;                ///< beyond letters, digits and `_`: "-" for CSS, "$" for JS
};

/// Every language, "Normal Text" first, the rest by name.
[[nodiscard]] std::span<const Language> languages();

/// "Normal Text": no highlighting.
[[nodiscard]] const Language& plainTextLanguage();

/// The language with `id`; nullptr if none.
[[nodiscard]] const Language* languageById(QStringView id);

/// The language for a file called `fileName` (a path is fine), by whole name, then by
/// extension; nullptr if it says nothing.
[[nodiscard]] const Language* languageForFileName(QStringView fileName);

/// The language a file's first line names: a `#!` line, `<?xml`, `<!DOCTYPE html>`;
/// nullptr if it says nothing.
[[nodiscard]] const Language* languageForFirstLine(QStringView firstLine);

/// languageForFileName(), else languageForFirstLine(), else plain text.
[[nodiscard]] const Language& detectLanguage(QStringView fileName, QStringView firstLine);

/// Whether `word` is in the sorted list `sorted` (case-insensitively if the language
/// is). Binary search, no allocation for a case-sensitive language.
[[nodiscard]] bool containsWord(const QStringList& sorted, QStringView word, bool caseInsensitive);

/// The file dialog filter for every language: "C++ (*.cpp *.hpp ...);;...", with
/// "All files (*)" first.
[[nodiscard]] QString fileDialogFilter();

} // namespace neko::pad
