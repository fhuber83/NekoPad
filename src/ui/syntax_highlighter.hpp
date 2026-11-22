#pragma once

#include "core/language.hpp"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace neko::pad::ui {

/// Syntax highlighting for every language of the table (core/language) but Markdown,
/// which has neko::editor::MarkdownHighlighter. One hand-written scanner per family --
/// no regular expressions -- that looks at each character once, so highlighting keeps
/// up with large files. Multi-line constructs (block comments, triple-quoted and
/// template strings, markup tags) carry over through the block state.
///
/// Colours come in a light and a dark set; setDark() switches and re-highlights.
///
/// UI thread only, like the document it highlights.
class SyntaxHighlighter final : public QSyntaxHighlighter {
public:
    SyntaxHighlighter(QTextDocument* document, const Language& language, bool dark);

    [[nodiscard]] const Language& language() const noexcept { return *language_; }
    void setDark(bool dark);

    /// The colours, for tests and the status bar's legend.
    struct Formats {
        QTextCharFormat keyword, type, constant, string, number, comment, preprocessor, variable, annotation, function,
            tag, attribute, entity, section, key, diffAdded, diffRemoved, diffHunk, diffHeader;
    };
    [[nodiscard]] const Formats& formats() const noexcept { return formats_; }

protected:
    void highlightBlock(const QString& text) override;

private:
    enum State : int {
        Normal = 0,
        InBlockComment = 1,
        InTripleDouble = 2,
        InTripleSingle = 3,
        InBacktick = 4,
        InTag = 6,
    };

    void buildFormats(bool dark);
    void highlightCode(const QString& text);
    void highlightMarkup(const QString& text);
    void highlightIni(const QString& text);
    void highlightDiff(const QString& text);

    /// Formats from `from` to the end of the string that `quote` opened at `from`
    /// (escapes skipped) and returns the index after it; text.size() if it runs to the
    /// end of the line.
    qsizetype scanString(const QString& text, qsizetype from, QChar quote, const QTextCharFormat& format);
    /// Formats up to and including `end` from `from`; returns the index after it, or -1
    /// if the line ends first (the rest is formatted).
    qsizetype scanUntil(const QString& text, qsizetype from, QStringView end, const QTextCharFormat& format);

    [[nodiscard]] bool isIdentChar(QChar c) const noexcept;

    const Language* language_;
    Formats formats_;
    bool json_ = false;
    bool yaml_ = false;
    bool wordComment_ = false;    ///< the line comment is a word ("REM"): only at the line's start
    bool hashNeedsSpace_ = false; ///< `#` starts a comment only after a blank (shell, Perl)
};

} // namespace neko::pad::ui
