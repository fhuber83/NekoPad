#include "core/line_ops.hpp"

#include <QSet>

#include <algorithm>
#include <limits>

namespace neko::pad::lines {

namespace {

[[nodiscard]] qsizetype indentationLength(QStringView line) {
    qsizetype i = 0;
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t')) {
        ++i;
    }
    return i;
}

[[nodiscard]] bool isBlank(QStringView line) {
    return indentationLength(line) == line.size();
}

/// Columns `indent` takes, tabs expanded.
[[nodiscard]] int indentColumns(QStringView indent, int tabWidth) {
    int columns = 0;
    for (const QChar c : indent) {
        columns = c == u'\t' ? (columns / tabWidth + 1) * tabWidth : columns + 1;
    }
    return columns;
}

} // namespace

QStringList sorted(QStringList lines, bool ascending, Qt::CaseSensitivity cs) {
    std::ranges::stable_sort(lines, [ascending, cs](const QString& a, const QString& b) {
        const int c = QString::compare(a, b, cs);
        return ascending ? c < 0 : c > 0;
    });
    return lines;
}

QStringList withoutDuplicates(const QStringList& lines) {
    QStringList out;
    out.reserve(lines.size());
    QSet<QString> seen;
    seen.reserve(lines.size());
    for (const QString& line : lines) {
        if (!seen.contains(line)) {
            seen.insert(line);
            out << line;
        }
    }
    return out;
}

QStringList withoutEmpty(const QStringList& lines, bool blankCountsAsEmpty) {
    QStringList out;
    out.reserve(lines.size());
    for (const QString& line : lines) {
        if (!(line.isEmpty() || (blankCountsAsEmpty && isBlank(line)))) {
            out << line;
        }
    }
    return out;
}

QStringList trimmedTrailing(QStringList lines) {
    for (QString& line : lines) {
        qsizetype end = line.size();
        while (end > 0 && line[end - 1].isSpace()) {
            --end;
        }
        line.truncate(end);
    }
    return lines;
}

QStringList trimmedLeading(QStringList lines) {
    for (QString& line : lines) {
        qsizetype start = 0;
        while (start < line.size() && line[start].isSpace()) {
            ++start;
        }
        line.remove(0, start);
    }
    return lines;
}

QStringList toggledLineComment(QStringList lines, const QString& token) {
    if (token.isEmpty()) {
        return lines;
    }
    bool allCommented = true;
    bool anyNonBlank = false;
    qsizetype minIndent = std::numeric_limits<qsizetype>::max();
    for (const QString& line : lines) {
        if (isBlank(line)) {
            continue;
        }
        anyNonBlank = true;
        const qsizetype indent = indentationLength(line);
        minIndent = std::min(minIndent, indent);
        if (!QStringView(line).sliced(indent).startsWith(token)) {
            allCommented = false;
        }
    }
    if (!anyNonBlank) {
        return lines;
    }
    for (QString& line : lines) {
        if (isBlank(line)) {
            continue;
        }
        if (allCommented) {
            const qsizetype at = indentationLength(line);
            qsizetype length = token.size();
            if (at + length < line.size() && line[at + length] == u' ') {
                ++length;
            }
            line.remove(at, length);
        } else {
            line.insert(minIndent, token + QLatin1Char(' '));
        }
    }
    return lines;
}

QStringList toggledBlockComment(QStringList lines, const QString& start, const QString& end) {
    if (start.isEmpty() || end.isEmpty()) {
        return lines;
    }
    bool allCommented = true;
    bool anyNonBlank = false;
    for (const QString& line : lines) {
        if (isBlank(line)) {
            continue;
        }
        anyNonBlank = true;
        const QStringView t = QStringView(line).trimmed();
        if (!t.startsWith(start) || !t.endsWith(end) || t.size() < start.size() + end.size()) {
            allCommented = false;
        }
    }
    if (!anyNonBlank) {
        return lines;
    }
    for (QString& line : lines) {
        if (isBlank(line)) {
            continue;
        }
        const qsizetype indent = indentationLength(line);
        if (allCommented) {
            qsizetype last = line.size();
            while (last > 0 && line[last - 1].isSpace()) {
                --last;
            }
            qsizetype endAt = last - end.size();
            if (endAt > 0 && line[endAt - 1] == u' ') {
                --endAt;
            }
            line.remove(endAt, last - endAt);
            qsizetype length = start.size();
            if (indent + length < line.size() && line[indent + length] == u' ') {
                ++length;
            }
            line.remove(indent, length);
        } else {
            line.insert(indent, start + QLatin1Char(' '));
            line.append(QLatin1Char(' ') + end);
        }
    }
    return lines;
}

QStringList indentationToSpaces(QStringList lines, int tabWidth) {
    tabWidth = std::max(1, tabWidth);
    for (QString& line : lines) {
        const qsizetype n = indentationLength(line);
        const int columns = indentColumns(QStringView(line).first(n), tabWidth);
        line.replace(0, n, QString(columns, u' '));
    }
    return lines;
}

QStringList indentationToTabs(QStringList lines, int tabWidth) {
    tabWidth = std::max(1, tabWidth);
    for (QString& line : lines) {
        const qsizetype n = indentationLength(line);
        const int columns = indentColumns(QStringView(line).first(n), tabWidth);
        line.replace(0, n, QString(columns / tabWidth, u'\t') + QString(columns % tabWidth, u' '));
    }
    return lines;
}

QString joined(const QStringList& lines) {
    QStringList parts;
    parts.reserve(lines.size());
    for (const QString& line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) {
            parts << t;
        }
    }
    return parts.join(QLatin1Char(' '));
}

int displayColumn(QStringView line, int positionInLine, int tabWidth) {
    tabWidth = std::max(1, tabWidth);
    int column = 0;
    const qsizetype end = std::min<qsizetype>(positionInLine, line.size());
    for (qsizetype i = 0; i < end; ++i) {
        column = line[i] == u'\t' ? (column / tabWidth + 1) * tabWidth : column + 1;
    }
    return column;
}

} // namespace neko::pad::lines
