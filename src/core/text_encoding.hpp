#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <array>
#include <cstdint>
#include <optional>

namespace neko::pad {

/// The encodings Neko Pad reads and writes. "BOM" variants write a byte-order mark.
enum class Encoding : std::uint8_t {
    Utf8,
    Utf8Bom,
    Utf16LeBom,
    Utf16BeBom,
    Latin1, ///< ISO 8859-1: every byte is a character, so any file decodes
};

inline constexpr std::array kAllEncodings{
    Encoding::Utf8, Encoding::Utf8Bom, Encoding::Utf16LeBom, Encoding::Utf16BeBom, Encoding::Latin1};

/// Line endings. The editor always holds `\n`; a document remembers what its file had
/// and writes that back.
enum class Eol : std::uint8_t {
    Lf,
    CrLf,
    Cr,
};

inline constexpr std::array kAllEols{Eol::CrLf, Eol::Lf, Eol::Cr};

/// The platform's convention, for new documents.
[[nodiscard]] constexpr Eol nativeEol() noexcept {
#ifdef Q_OS_WIN
    return Eol::CrLf;
#else
    return Eol::Lf;
#endif
}

[[nodiscard]] QString encodingName(Encoding encoding);
/// A stable identifier for settings ("utf-8", "utf-8-bom", ...), and back.
[[nodiscard]] QString encodingId(Encoding encoding);
[[nodiscard]] std::optional<Encoding> encodingFromId(QStringView id);

[[nodiscard]] QString eolName(Eol eol);      ///< "Windows (CR LF)", ...
[[nodiscard]] QString eolShortName(Eol eol); ///< "CRLF", "LF", "CR"
[[nodiscard]] QStringView eolSequence(Eol eol);

/// What decode() made of a file's bytes.
struct DecodedText {
    QString text; ///< line breaks normalized to `\n`
    Encoding encoding = Encoding::Utf8;
    Eol eol = nativeEol();    ///< the most frequent line ending; nativeEol() with none
    bool mixedEols = false;   ///< more than one kind of line ending was found
    bool looksBinary = false; ///< NUL bytes in a file without a UTF-16 BOM
    qsizetype lineCount = 1;
};

/// The encoding of `bytes`: a byte-order mark decides; else valid UTF-8 is UTF-8; else
/// Latin-1. Fast: the UTF-8 check is one pass without allocating.
[[nodiscard]] Encoding detectEncoding(QByteArrayView bytes) noexcept;

/// Whether `bytes` is well-formed UTF-8 (no overlongs, no surrogates, nothing past
/// U+10FFFF). Checks eight bytes at a time while they are ASCII.
[[nodiscard]] bool isValidUtf8(QByteArrayView bytes) noexcept;

/// Decodes `bytes` -- in `forced` if given, else in detectEncoding()'s guess -- and
/// normalizes the line breaks. Pure; any thread.
[[nodiscard]] DecodedText decode(QByteArrayView bytes, std::optional<Encoding> forced = std::nullopt);

/// The most frequent line ending in `text` (nativeEol() without any), and whether there
/// were several kinds.
struct EolStats {
    Eol eol = nativeEol();
    bool mixed = false;
    qsizetype lines = 1;
};
[[nodiscard]] EolStats detectEol(QStringView text) noexcept;

/// `text` with every CR LF and lone CR turned into LF. Returns `text` itself (shared,
/// no copy) when it has no CR.
[[nodiscard]] QString normalizeEols(QString text);

/// `text` (`\n` line breaks) in `encoding` with `eol` line breaks, the BOM included.
[[nodiscard]] QByteArray encode(QStringView text, Encoding encoding, Eol eol);

/// Whether `encoding` can hold every character of `text` (only Latin-1 cannot).
[[nodiscard]] bool canEncode(QStringView text, Encoding encoding) noexcept;

} // namespace neko::pad
