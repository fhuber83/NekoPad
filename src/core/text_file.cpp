#include "core/text_file.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace neko::pad {

namespace {

[[nodiscard]] QString tr(const char* text) {
    return QCoreApplication::translate("neko::pad::TextFile", text);
}

} // namespace

LoadedFile loadTextFile(const QString& path, std::optional<Encoding> forced) {
    LoadedFile result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    const QFileInfo info(file);
    result.size = file.size();
    result.modified = info.lastModified();

    if (result.size == 0) {
        result.ok = true;
        result.decoded = decode({}, forced);
        return result;
    }

    // Mapping saves a copy of the whole file; fall back to reading where it fails
    // (pipes, some network file systems).
    if (uchar* mapped = file.map(0, result.size)) {
        result.decoded = decode(QByteArrayView(reinterpret_cast<const char*>(mapped), result.size), forced);
        file.unmap(mapped);
    } else {
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            result.error = file.errorString();
            return result;
        }
        result.decoded = decode(bytes, forced);
    }
    result.ok = true;
    return result;
}

SaveResult saveTextFile(const QString& path, QStringView text, Encoding encoding, Eol eol) {
    SaveResult result;
    const QByteArray bytes = encode(text, encoding, eol);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.write(bytes) != bytes.size()) {
        result.error = file.errorString();
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.error = file.errorString().isEmpty() ? tr("The file could not be written.") : file.errorString();
        return result;
    }
    result.ok = true;
    result.modified = QFileInfo(path).lastModified();
    return result;
}

QString formatByteSize(qint64 bytes) {
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    static constexpr const char* kUnits[] = {"KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1StringView(kUnits[unit]));
}

} // namespace neko::pad
