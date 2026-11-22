#pragma once

#include "core/text_encoding.hpp"

#include <QDateTime>
#include <QString>

#include <optional>

namespace neko::pad {

/// A file read and decoded by loadTextFile().
struct LoadedFile {
    bool ok = false;
    QString error; ///< why not, when !ok
    DecodedText decoded;
    qint64 size = 0;    ///< bytes on disk
    QDateTime modified; ///< the file's modification time when read
};

/// Reads and decodes `path` (memory-mapped where the platform allows). Pure apart from
/// the read; meant for a worker thread. `forced` decodes in that encoding instead of
/// the detected one.
[[nodiscard]] LoadedFile loadTextFile(const QString& path, std::optional<Encoding> forced = std::nullopt);

struct SaveResult {
    bool ok = false;
    QString error;
    QDateTime modified; ///< the file's modification time after writing
};

/// Writes `text` to `path` atomically (QSaveFile: a temporary file, renamed over the
/// old one only once it is complete).
[[nodiscard]] SaveResult saveTextFile(const QString& path, QStringView text, Encoding encoding, Eol eol);

/// A size for people: "512 B", "12.3 KiB", "4.0 MiB".
[[nodiscard]] QString formatByteSize(qint64 bytes);

} // namespace neko::pad
