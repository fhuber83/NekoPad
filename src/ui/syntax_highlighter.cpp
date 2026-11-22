#include "ui/syntax_highlighter.hpp"

#include <QFont>

namespace neko::pad::ui {

namespace {

[[nodiscard]] QTextCharFormat makeFormat(const char* color, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(QColor(QLatin1StringView(color)));
    if (bold) {
        f.setFontWeight(QFont::Bold);
    }
    if (italic) {
        f.setFontItalic(true);
    }
    return f;
}

[[nodiscard]] bool isAsciiDigit(QChar c) noexcept {
    return c >= u'0' && c <= u'9';
}

[[nodiscard]] qsizetype firstNonSpace(const QString& text) noexcept {
    qsizetype i = 0;
    while (i < text.size() && (text[i] == u' ' || text[i] == u'\t')) {
        ++i;
    }
    return i;
}

} // namespace

SyntaxHighlighter::SyntaxHighlighter(QTextDocument* document, const Language& language, bool dark)
    : QSyntaxHighlighter(document), // the document's child: it goes where the document goes
      language_(&language) {
    json_ = language.id == u"json";
    yaml_ = language.id == u"yaml";
    wordComment_ = !language.lineComment.isEmpty() && language.lineComment.front().isLetter();
    hashNeedsSpace_ = language.lineComment == u"#" && language.variables;
    buildFormats(dark); // before the first pass, which QSyntaxHighlighter schedules for later
}

void SyntaxHighlighter::setDark(bool dark) {
    buildFormats(dark);
    rehighlight();
}

void SyntaxHighlighter::buildFormats(bool dark) {
    Formats& f = formats_;
    if (dark) {
        f.keyword = makeFormat("#569CD6", true);
        f.type = makeFormat("#4EC9B0");
        f.constant = makeFormat("#4FC1FF");
        f.string = makeFormat("#CE9178");
        f.number = makeFormat("#B5CEA8");
        f.comment = makeFormat("#6A9955", false, true);
        f.preprocessor = makeFormat("#C586C0");
        f.variable = makeFormat("#9CDCFE");
        f.annotation = makeFormat("#DCDCAA");
        f.function = makeFormat("#DCDCAA");
        f.tag = makeFormat("#569CD6");
        f.attribute = makeFormat("#9CDCFE");
        f.entity = makeFormat("#D7BA7D");
        f.section = makeFormat("#569CD6", true);
        f.key = makeFormat("#9CDCFE");
        f.diffAdded = makeFormat("#7EE787");
        f.diffRemoved = makeFormat("#FFA198");
        f.diffHunk = makeFormat("#D2A8FF");
        f.diffHeader = makeFormat("#E6EDF3", true);
    } else {
        f.keyword = makeFormat("#0000C0", true);
        f.type = makeFormat("#267F99");
        f.constant = makeFormat("#0451A5");
        f.string = makeFormat("#A31515");
        f.number = makeFormat("#098658");
        f.comment = makeFormat("#008000", false, true);
        f.preprocessor = makeFormat("#AF00DB");
        f.variable = makeFormat("#001080");
        f.annotation = makeFormat("#808000");
        f.function = makeFormat("#795E26");
        f.tag = makeFormat("#800000");
        f.attribute = makeFormat("#C50000");
        f.entity = makeFormat("#0451A5");
        f.section = makeFormat("#0000C0", true);
        f.key = makeFormat("#001080");
        f.diffAdded = makeFormat("#116329");
        f.diffRemoved = makeFormat("#82071E");
        f.diffHunk = makeFormat("#6F42C1");
        f.diffHeader = makeFormat("#1F2328", true);
    }
}

void SyntaxHighlighter::highlightBlock(const QString& text) {
    switch (language_->family) {
    case LanguageFamily::Code:
        highlightCode(text);
        break;
    case LanguageFamily::Markup:
        highlightMarkup(text);
        break;
    case LanguageFamily::Ini:
        highlightIni(text);
        break;
    case LanguageFamily::Diff:
        highlightDiff(text);
        break;
    case LanguageFamily::Plain:
    case LanguageFamily::Markdown:
        break;
    }
}

bool SyntaxHighlighter::isIdentChar(QChar c) const noexcept {
    return c.isLetterOrNumber() || c == u'_' || language_->extraIdentChars.contains(c);
}

qsizetype SyntaxHighlighter::scanString(const QString& text, qsizetype from, QChar quote, const QTextCharFormat& f) {
    qsizetype i = from + 1;
    const qsizetype n = text.size();
    while (i < n) {
        const QChar c = text[i];
        if (c == u'\\') {
            i += 2;
            continue;
        }
        ++i;
        if (c == quote) {
            break;
        }
    }
    i = std::min(i, n);
    setFormat(static_cast<int>(from), static_cast<int>(i - from), f);
    return i;
}

qsizetype SyntaxHighlighter::scanUntil(const QString& text, qsizetype from, QStringView end, const QTextCharFormat& f) {
    const qsizetype at = text.indexOf(end, from);
    if (at < 0) {
        setFormat(static_cast<int>(from), static_cast<int>(text.size() - from), f);
        return -1;
    }
    const qsizetype stop = at + end.size();
    setFormat(static_cast<int>(from), static_cast<int>(stop - from), f);
    return stop;
}

void SyntaxHighlighter::highlightCode(const QString& text) {
    const Language& l = *language_;
    const qsizetype n = text.size();
    qsizetype i = 0;
    setCurrentBlockState(Normal);

    // Carry-over from the line before.
    switch (previousBlockState()) {
    case InBlockComment:
        i = scanUntil(text, 0, l.blockEnd, formats_.comment);
        if (i < 0) {
            setCurrentBlockState(InBlockComment);
            return;
        }
        break;
    case InTripleDouble:
    case InTripleSingle: {
        const QStringView end = previousBlockState() == InTripleDouble ? u"\"\"\"" : u"'''";
        i = scanUntil(text, 0, end, formats_.string);
        if (i < 0) {
            setCurrentBlockState(previousBlockState());
            return;
        }
        break;
    }
    case InBacktick: {
        qsizetype j = 0;
        while (j < n && text[j] != u'`') {
            j += text[j] == u'\\' ? 2 : 1;
        }
        if (j >= n) {
            setFormat(0, static_cast<int>(n), formats_.string);
            setCurrentBlockState(InBacktick);
            return;
        }
        setFormat(0, static_cast<int>(j + 1), formats_.string);
        i = j + 1;
        break;
    }
    default:
        break;
    }

    const qsizetype indent = firstNonSpace(text);

    // Preprocessor directives own their line, up to a comment.
    if (l.preprocessor && i == 0 && indent < n && text[indent] == u'#') {
        qsizetype end = text.indexOf(u"//", indent);
        if (end < 0) {
            end = n;
        }
        setFormat(static_cast<int>(indent), static_cast<int>(end - indent), formats_.preprocessor);
        i = end;
    }

    // YAML keys: "key:" / "- key:" at the start of a line.
    if (yaml_ && i == 0) {
        qsizetype k = indent;
        if (k < n && text[k] == u'-') {
            k = k + 1;
            while (k < n && text[k] == u' ') {
                ++k;
            }
        }
        qsizetype colon = k;
        while (colon < n && text[colon] != u':' && text[colon] != u'#' && text[colon] != u'"' && text[colon] != u'\'') {
            ++colon;
        }
        if (colon < n && text[colon] == u':' && colon > k && (colon + 1 == n || text[colon + 1] == u' ')) {
            setFormat(static_cast<int>(k), static_cast<int>(colon - k), formats_.key);
            i = colon + 1;
        }
    }

    while (i < n) {
        const QChar c = text[i];

        // Line comment.
        if (!l.lineComment.isEmpty() && QStringView(text).sliced(i).startsWith(l.lineComment, Qt::CaseInsensitive)) {
            bool isComment = true;
            if (wordComment_) {
                const qsizetype after = i + l.lineComment.size();
                isComment = i == indent && (after >= n || !isIdentChar(text[after]));
            } else if (hashNeedsSpace_) {
                isComment = i == 0 || text[i - 1].isSpace();
            } else if (!l.caseInsensitive && !QStringView(text).sliced(i).startsWith(l.lineComment)) {
                isComment = false;
            }
            // Batch also has "::" comments.
            if (isComment) {
                setFormat(static_cast<int>(i), static_cast<int>(n - i), formats_.comment);
                return;
            }
        }
        if (wordComment_ && i == indent && QStringView(text).sliced(i).startsWith(u"::")) {
            setFormat(static_cast<int>(i), static_cast<int>(n - i), formats_.comment);
            return;
        }

        // Block comment.
        if (!l.blockStart.isEmpty() && QStringView(text).sliced(i).startsWith(l.blockStart)) {
            // The end is searched after the start, so "/*/" does not close itself.
            setFormat(static_cast<int>(i), static_cast<int>(l.blockStart.size()), formats_.comment);
            const qsizetype end = scanUntil(text, i + l.blockStart.size(), l.blockEnd, formats_.comment);
            if (end < 0) {
                setCurrentBlockState(InBlockComment);
                return;
            }
            i = end;
            continue;
        }

        // Triple-quoted strings.
        if (l.tripleQuotes && (c == u'"' || c == u'\'') && i + 2 < n && text[i + 1] == c && text[i + 2] == c) {
            const QStringView end = c == u'"' ? u"\"\"\"" : u"'''";
            const qsizetype stop = scanUntil(text, i + 3, end, formats_.string);
            setFormat(static_cast<int>(i), 3, formats_.string);
            if (stop < 0) {
                setCurrentBlockState(c == u'"' ? InTripleDouble : InTripleSingle);
                return;
            }
            i = stop;
            continue;
        }

        // Strings.
        if (l.quotes.contains(c)) {
            if (c == u'`' && l.backtickMultiline) {
                qsizetype j = i + 1;
                while (j < n && text[j] != u'`') {
                    j += text[j] == u'\\' ? 2 : 1;
                }
                if (j >= n) {
                    setFormat(static_cast<int>(i), static_cast<int>(n - i), formats_.string);
                    setCurrentBlockState(InBacktick);
                    return;
                }
                setFormat(static_cast<int>(i), static_cast<int>(j + 1 - i), formats_.string);
                i = j + 1;
                continue;
            }
            const qsizetype stop = scanString(text, i, c, formats_.string);
            if (json_) {
                // A string followed by ':' is a key.
                qsizetype k = stop;
                while (k < n && text[k] == u' ') {
                    ++k;
                }
                if (k < n && text[k] == u':') {
                    setFormat(static_cast<int>(i), static_cast<int>(stop - i), formats_.key);
                }
            }
            i = stop;
            continue;
        }

        // Numbers (not the digits inside an identifier).
        if ((isAsciiDigit(c) || (c == u'.' && i + 1 < n && isAsciiDigit(text[i + 1]))) &&
            (i == 0 || !isIdentChar(text[i - 1]))) {
            qsizetype j = i + 1;
            while (j < n) {
                const QChar d = text[j];
                if (d.isLetterOrNumber() || d == u'_' || d == u'\'' ||
                    (d == u'.' && j + 1 < n && isAsciiDigit(text[j + 1]))) {
                    ++j;
                } else if (
                    (d == u'+' || d == u'-') && (text[j - 1] == u'e' || text[j - 1] == u'E') &&
                    !text.sliced(i, j - i).startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                    ++j;
                } else {
                    break;
                }
            }
            setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.number);
            i = j;
            continue;
        }

        // Variables: $name, ${...}, %name%.
        if (l.variables && (c == u'$' || (l.caseInsensitive && wordComment_ && c == u'%'))) {
            qsizetype j = i + 1;
            if (c == u'%') {
                if (j < n && text[j] == u'%') {
                    ++j; // %%i
                }
                const qsizetype close = text.indexOf(u'%', j);
                if (j < n && isAsciiDigit(text[j])) {
                    j += 1;
                } else if (j < n && (text[j] == u'~' || isIdentChar(text[j])) && close > j) {
                    const qsizetype space = text.indexOf(u' ', j);
                    j = (space < 0 || space > close) ? close + 1 : j + 1;
                }
            } else if (j < n && (text[j] == u'{' || text[j] == u'(')) {
                const QChar closer = text[j] == u'{' ? u'}' : u')';
                const qsizetype close = text.indexOf(closer, j);
                j = close < 0 ? n : close + 1;
            } else {
                while (j < n && (isIdentChar(text[j]) || text[j] == u':')) {
                    ++j;
                }
                if (j == i + 1 && j < n && QStringLiteral("@#?*!$0123456789").contains(text[j])) {
                    ++j;
                }
            }
            if (j > i + 1) {
                setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.variable);
                i = j;
                continue;
            }
        }

        // Annotations and decorators.
        if (l.annotations && c == u'@' && i + 1 < n && text[i + 1].isLetter()) {
            qsizetype j = i + 1;
            while (j < n && (isIdentChar(text[j]) || text[j] == u'.')) {
                ++j;
            }
            setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.annotation);
            i = j;
            continue;
        }

        // Words.
        if (c.isLetter() || c == u'_' || (l.extraIdentChars.contains(c) && c != u'-')) {
            qsizetype j = i + 1;
            while (j < n && isIdentChar(text[j])) {
                ++j;
            }
            // Ruby's "defined?"
            const QStringView word = QStringView(text).sliced(i, j - i);
            const bool ci = l.caseInsensitive;
            if (containsWord(l.keywords, word, ci)) {
                setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.keyword);
            } else if (containsWord(l.types, word, ci)) {
                setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.type);
            } else if (containsWord(l.constants, word, ci)) {
                setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.constant);
            } else {
                qsizetype k = j;
                while (k < n && text[k] == u' ') {
                    ++k;
                }
                if (k < n && text[k] == u'(') {
                    setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.function);
                }
            }
            i = j;
            continue;
        }
        ++i;
    }
}

void SyntaxHighlighter::highlightMarkup(const QString& text) {
    const qsizetype n = text.size();
    qsizetype i = 0;
    int state = previousBlockState() < 0 ? Normal : previousBlockState();
    setCurrentBlockState(Normal);

    auto isNameChar = [](QChar c) {
        return c.isLetterOrNumber() || c == u'_' || c == u'-' || c == u':' || c == u'.';
    };

    while (i < n) {
        if (state == InBlockComment) {
            const qsizetype end = scanUntil(text, i, u"-->", formats_.comment);
            if (end < 0) {
                setCurrentBlockState(InBlockComment);
                return;
            }
            i = end;
            state = Normal;
            continue;
        }
        if (state == InTag) {
            const QChar c = text[i];
            if (c == u'>') {
                setFormat(static_cast<int>(i), 1, formats_.tag);
                ++i;
                state = Normal;
            } else if ((c == u'/' || c == u'?') && i + 1 < n && text[i + 1] == u'>') {
                setFormat(static_cast<int>(i), 2, formats_.tag);
                i += 2;
                state = Normal;
            } else if (c == u'"' || c == u'\'') {
                const qsizetype close = text.indexOf(c, i + 1);
                const qsizetype stop = close < 0 ? n : close + 1;
                setFormat(static_cast<int>(i), static_cast<int>(stop - i), formats_.string);
                i = stop;
            } else if (isNameChar(c)) {
                qsizetype j = i + 1;
                while (j < n && isNameChar(text[j])) {
                    ++j;
                }
                setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.attribute);
                i = j;
            } else {
                ++i;
            }
            continue;
        }
        // Normal text.
        const qsizetype lt = text.indexOf(u'<', i);
        const qsizetype amp = text.indexOf(u'&', i);
        qsizetype next = n;
        if (lt >= 0) {
            next = lt;
        }
        if (amp >= 0 && amp < next) {
            next = amp;
        }
        if (next >= n) {
            break;
        }
        i = next;
        if (text[i] == u'&') {
            qsizetype j = i + 1;
            while (j < n && j - i < 12 && (text[j].isLetterOrNumber() || text[j] == u'#')) {
                ++j;
            }
            if (j < n && text[j] == u';' && j > i + 1) {
                setFormat(static_cast<int>(i), static_cast<int>(j + 1 - i), formats_.entity);
                i = j + 1;
            } else {
                ++i;
            }
            continue;
        }
        if (QStringView(text).sliced(i).startsWith(u"<!--")) {
            state = InBlockComment;
            continue;
        }
        if (QStringView(text).sliced(i).startsWith(u"<![CDATA[")) {
            const qsizetype end = text.indexOf(u"]]>", i);
            const qsizetype stop = end < 0 ? n : end + 3;
            setFormat(static_cast<int>(i), static_cast<int>(stop - i), formats_.string);
            i = stop;
            continue;
        }
        if (i + 1 < n &&
            (text[i + 1].isLetter() || text[i + 1] == u'/' || text[i + 1] == u'!' || text[i + 1] == u'?')) {
            qsizetype j = i + 1;
            if (text[j] == u'/' || text[j] == u'!' || text[j] == u'?') {
                ++j;
            }
            while (j < n && isNameChar(text[j])) {
                ++j;
            }
            setFormat(static_cast<int>(i), static_cast<int>(j - i), formats_.tag);
            i = j;
            state = InTag;
            continue;
        }
        ++i;
    }
    if (state == InTag) {
        setCurrentBlockState(InTag);
    }
}

void SyntaxHighlighter::highlightIni(const QString& text) {
    const qsizetype n = text.size();
    const qsizetype i = firstNonSpace(text);
    if (i >= n) {
        return;
    }
    const QChar c = text[i];
    if (c == u'#' || c == u';') {
        setFormat(static_cast<int>(i), static_cast<int>(n - i), formats_.comment);
        return;
    }
    if (c == u'[') {
        const qsizetype close = text.lastIndexOf(u']');
        const qsizetype stop = close < 0 ? n : close + 1;
        setFormat(static_cast<int>(i), static_cast<int>(stop - i), formats_.section);
        return;
    }
    qsizetype sep = i;
    while (sep < n && text[sep] != u'=' && text[sep] != u':') {
        ++sep;
    }
    if (sep >= n) {
        return;
    }
    setFormat(static_cast<int>(i), static_cast<int>(sep - i), formats_.key);
    // The value: a string, a number, a boolean -- or plain.
    qsizetype v = sep + 1;
    while (v < n && text[v] == u' ') {
        ++v;
    }
    if (v >= n) {
        return;
    }
    if (text[v] == u'"' || text[v] == u'\'') {
        (void)scanString(text, v, text[v], formats_.string);
        return;
    }
    const QStringView value = QStringView(text).sliced(v).trimmed();
    if (value == u"true" || value == u"false" || value == u"yes" || value == u"no" || value == u"on" ||
        value == u"off") {
        setFormat(static_cast<int>(v), static_cast<int>(value.size()), formats_.constant);
    } else if (!value.isEmpty() && (isAsciiDigit(value.front()) || value.front() == u'-')) {
        bool numeric = true;
        for (const QChar d : value) {
            if (!(isAsciiDigit(d) || d == u'.' || d == u'-' || d == u'_' || d == u'e' || d == u'E' || d == u'+')) {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            setFormat(static_cast<int>(v), static_cast<int>(value.size()), formats_.number);
        }
    }
}

void SyntaxHighlighter::highlightDiff(const QString& text) {
    const int n = static_cast<int>(text.size());
    if (text.startsWith(u"+++") || text.startsWith(u"---") || text.startsWith(u"diff ") || text.startsWith(u"index ")) {
        setFormat(0, n, formats_.diffHeader);
    } else if (text.startsWith(u"@@")) {
        setFormat(0, n, formats_.diffHunk);
    } else if (text.startsWith(u'+')) {
        setFormat(0, n, formats_.diffAdded);
    } else if (text.startsWith(u'-')) {
        setFormat(0, n, formats_.diffRemoved);
    }
}

} // namespace neko::pad::ui
