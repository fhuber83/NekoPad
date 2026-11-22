#include "core/text_encoding.hpp"

#include <QCoreApplication>
#include <QStringDecoder>
#include <QStringEncoder>

#include <algorithm>
#include <cstring>

namespace neko::pad {

namespace {

constexpr unsigned char kUtf8Bom[] = {0xEF, 0xBB, 0xBF};

[[nodiscard]] bool startsWith(QByteArrayView bytes, std::initializer_list<unsigned char> prefix) noexcept {
    if (bytes.size() < static_cast<qsizetype>(prefix.size())) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), reinterpret_cast<const unsigned char*>(bytes.data()));
}

[[nodiscard]] bool inRange(unsigned char c, unsigned char lo, unsigned char hi) noexcept {
    return c >= lo && c <= hi;
}

[[nodiscard]] QString tr(const char* text) {
    return QCoreApplication::translate("neko::pad::Encoding", text);
}

} // namespace

QString encodingName(Encoding encoding) {
    switch (encoding) {
    case Encoding::Utf8:
        return QStringLiteral("UTF-8");
    case Encoding::Utf8Bom:
        return QStringLiteral("UTF-8 BOM");
    case Encoding::Utf16LeBom:
        return QStringLiteral("UTF-16 LE BOM");
    case Encoding::Utf16BeBom:
        return QStringLiteral("UTF-16 BE BOM");
    case Encoding::Latin1:
        return QStringLiteral("ISO 8859-1 (Latin-1)");
    }
    return {};
}

QString encodingId(Encoding encoding) {
    switch (encoding) {
    case Encoding::Utf8:
        return QStringLiteral("utf-8");
    case Encoding::Utf8Bom:
        return QStringLiteral("utf-8-bom");
    case Encoding::Utf16LeBom:
        return QStringLiteral("utf-16le-bom");
    case Encoding::Utf16BeBom:
        return QStringLiteral("utf-16be-bom");
    case Encoding::Latin1:
        return QStringLiteral("latin-1");
    }
    return {};
}

std::optional<Encoding> encodingFromId(QStringView id) {
    for (const Encoding e : kAllEncodings) {
        if (id == encodingId(e)) {
            return e;
        }
    }
    return std::nullopt;
}

QString eolName(Eol eol) {
    switch (eol) {
    case Eol::CrLf:
        return tr("Windows (CR LF)");
    case Eol::Lf:
        return tr("Unix (LF)");
    case Eol::Cr:
        return tr("Macintosh (CR)");
    }
    return {};
}

QString eolShortName(Eol eol) {
    switch (eol) {
    case Eol::CrLf:
        return QStringLiteral("CRLF");
    case Eol::Lf:
        return QStringLiteral("LF");
    case Eol::Cr:
        return QStringLiteral("CR");
    }
    return {};
}

QStringView eolSequence(Eol eol) {
    switch (eol) {
    case Eol::CrLf:
        return u"\r\n";
    case Eol::Lf:
        return u"\n";
    case Eol::Cr:
        return u"\r";
    }
    return u"\n";
}

bool isValidUtf8(QByteArrayView bytes) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const auto* const end = p + bytes.size();
    while (p < end) {
        // ASCII fast path, eight bytes at a time.
        if (end - p >= 8) {
            std::uint64_t word = 0;
            std::memcpy(&word, p, sizeof word);
            if ((word & 0x8080808080808080ULL) == 0) {
                p += 8;
                continue;
            }
        }
        const unsigned char c = *p;
        if (c < 0x80) {
            ++p;
            continue;
        }
        if (c < 0xC2) {
            return false; // a continuation byte, or an overlong two-byte form
        }
        if (c < 0xE0) {
            if (end - p < 2 || !inRange(p[1], 0x80, 0xBF)) {
                return false;
            }
            p += 2;
        } else if (c < 0xF0) {
            if (end - p < 3) {
                return false;
            }
            const unsigned char lo = c == 0xE0 ? 0xA0 : 0x80; // no overlongs
            const unsigned char hi = c == 0xED ? 0x9F : 0xBF; // no surrogates
            if (!inRange(p[1], lo, hi) || !inRange(p[2], 0x80, 0xBF)) {
                return false;
            }
            p += 3;
        } else if (c < 0xF5) {
            if (end - p < 4) {
                return false;
            }
            const unsigned char lo = c == 0xF0 ? 0x90 : 0x80; // no overlongs
            const unsigned char hi = c == 0xF4 ? 0x8F : 0xBF; // nothing past U+10FFFF
            if (!inRange(p[1], lo, hi) || !inRange(p[2], 0x80, 0xBF) || !inRange(p[3], 0x80, 0xBF)) {
                return false;
            }
            p += 4;
        } else {
            return false;
        }
    }
    return true;
}

Encoding detectEncoding(QByteArrayView bytes) noexcept {
    if (startsWith(bytes, {0xEF, 0xBB, 0xBF})) {
        return Encoding::Utf8Bom;
    }
    if (startsWith(bytes, {0xFF, 0xFE})) {
        return Encoding::Utf16LeBom;
    }
    if (startsWith(bytes, {0xFE, 0xFF})) {
        return Encoding::Utf16BeBom;
    }
    return isValidUtf8(bytes) ? Encoding::Utf8 : Encoding::Latin1;
}

EolStats detectEol(QStringView text) noexcept {
    const char16_t* p = text.utf16();
    const char16_t* const end = p + text.size();
    qsizetype lf = 0;
    qsizetype crlf = 0;
    qsizetype cr = 0;
    // Most files have no CR at all: then it is a count of LFs, which vectorizes.
    const char16_t* firstCr = std::find(p, end, u'\r');
    lf = std::count(p, firstCr, u'\n');
    for (p = firstCr; p < end; ++p) {
        if (*p == u'\n') {
            ++lf;
        } else if (*p == u'\r') {
            if (p + 1 < end && p[1] == u'\n') {
                ++crlf;
                ++p;
            } else {
                ++cr;
            }
        }
    }
    EolStats stats;
    stats.lines = lf + crlf + cr + 1;
    const int kinds = (lf > 0) + (crlf > 0) + (cr > 0);
    stats.mixed = kinds > 1;
    if (kinds == 0) {
        stats.eol = nativeEol();
    } else if (crlf >= lf && crlf >= cr) {
        stats.eol = Eol::CrLf;
    } else if (lf >= cr) {
        stats.eol = Eol::Lf;
    } else {
        stats.eol = Eol::Cr;
    }
    return stats;
}

QString normalizeEols(QString text) {
    const qsizetype firstCr = text.indexOf(u'\r');
    if (firstCr < 0) {
        return text;
    }
    char16_t* const data = reinterpret_cast<char16_t*>(text.data()); // detaches once
    const qsizetype size = text.size();
    qsizetype out = firstCr;
    for (qsizetype in = firstCr; in < size; ++in) {
        const char16_t c = data[in];
        if (c == u'\r') {
            data[out++] = u'\n';
            if (in + 1 < size && data[in + 1] == u'\n') {
                ++in;
            }
        } else {
            data[out++] = c;
        }
    }
    text.truncate(out);
    return text;
}

DecodedText decode(QByteArrayView bytes, std::optional<Encoding> forced) {
    DecodedText result;
    const Encoding detected = detectEncoding(bytes);
    result.encoding = forced.value_or(detected);

    // Skip a BOM that matches the encoding we decode in.
    auto skipBom = [&](qsizetype n, Encoding bomEncoding) {
        return detected == bomEncoding ? bytes.sliced(n) : bytes;
    };

    switch (result.encoding) {
    case Encoding::Utf8:
    case Encoding::Utf8Bom: {
        const QByteArrayView body = skipBom(sizeof kUtf8Bom, Encoding::Utf8Bom);
        // ConvertInitialBom: a U+FEFF left after the file's BOM is text (fromUtf8 would
        // drop it as a second BOM).
        QStringDecoder decoder(
            QStringConverter::Utf8, QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
        result.text = decoder.decode(body);
        break;
    }
    case Encoding::Utf16LeBom:
    case Encoding::Utf16BeBom: {
        const Encoding bom = result.encoding;
        const QByteArrayView body = skipBom(2, bom);
        QStringDecoder decoder(
            bom == Encoding::Utf16LeBom ? QStringConverter::Utf16LE : QStringConverter::Utf16BE,
            QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
        result.text = decoder.decode(body);
        break;
    }
    case Encoding::Latin1:
        result.text = QString::fromLatin1(bytes);
        break;
    }

    if (result.encoding != Encoding::Utf16LeBom && result.encoding != Encoding::Utf16BeBom) {
        result.looksBinary = std::memchr(bytes.data(), 0, static_cast<std::size_t>(bytes.size())) != nullptr;
    }

    const EolStats stats = detectEol(result.text);
    result.eol = stats.eol;
    result.mixedEols = stats.mixed;
    result.lineCount = stats.lines;
    result.text = normalizeEols(std::move(result.text));
    return result;
}

QByteArray encode(QStringView text, Encoding encoding, Eol eol) {
    QString withEols;
    QStringView source = text;
    if (eol != Eol::Lf && text.contains(u'\n')) {
        withEols = text.toString();
        withEols.replace(QChar(u'\n'), eolSequence(eol).toString());
        source = withEols;
    }

    switch (encoding) {
    case Encoding::Utf8:
        return source.toUtf8();
    case Encoding::Utf8Bom: {
        QByteArray out;
        out.reserve(source.size() + 3);
        out.append(reinterpret_cast<const char*>(kUtf8Bom), sizeof kUtf8Bom);
        out.append(source.toUtf8());
        return out;
    }
    case Encoding::Utf16LeBom:
    case Encoding::Utf16BeBom: {
        const bool le = encoding == Encoding::Utf16LeBom;
        QStringEncoder encoder(le ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
        QByteArray out = le ? QByteArray("\xFF\xFE", 2) : QByteArray("\xFE\xFF", 2);
        out.append(encoder.encode(source));
        return out;
    }
    case Encoding::Latin1:
        return source.toLatin1();
    }
    return {};
}

bool canEncode(QStringView text, Encoding encoding) noexcept {
    if (encoding != Encoding::Latin1) {
        return true;
    }
    return std::ranges::all_of(text, [](QChar c) { return c.unicode() <= 0xFF; });
}

} // namespace neko::pad
