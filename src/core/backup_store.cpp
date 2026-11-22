#include "core/backup_store.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

namespace neko::pad {

namespace {

/// `name` with only characters every file system takes, and not too long.
[[nodiscard]] QString safeFileName(const QString& name) {
    QString safe;
    safe.reserve(std::min<qsizetype>(name.size(), 64));
    for (const QChar c : name.first(std::min<qsizetype>(name.size(), 64))) {
        const bool ok = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'.' ||
                        c == u'-' || c == u'_' || c == u' ';
        safe += ok ? c : QChar(u'_');
    }
    safe = safe.trimmed();
    return safe.isEmpty() ? QStringLiteral("untitled") : safe;
}

} // namespace

BackupStore::BackupStore(QString directory)
    : directory_(std::move(directory)) {
    pool_.setMaxThreadCount(1);
    pool_.setObjectName(QStringLiteral("BackupStore"));
}

BackupStore::~BackupStore() {
    pool_.waitForDone();
}

QString BackupStore::newPath(const QString& name) {
    const QDir dir(directory_);
    const QString base =
        safeFileName(name) + u'@' + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    for (int n = 0;; ++n) {
        const QString path = dir.filePath(n == 0 ? base : QStringLiteral("%1-%2").arg(base).arg(n));
        if (!handedOut_.contains(path) && !QFileInfo::exists(path)) {
            handedOut_.insert(path);
            return path;
        }
    }
}

void BackupStore::write(const QString& path, QString text) {
    pool_.start([this, path, text = std::move(text)] {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const SaveResult result = saveTextFile(path, text, Encoding::Utf8Bom, Eol::Lf);
        const QMutexLocker lock(&mutex_);
        if (result.ok) {
            failed_.remove(path);
        } else {
            qWarning(
                "Neko Pad: the backup %s could not be written: %s",
                qPrintable(QDir::toNativeSeparators(path)),
                qPrintable(result.error));
            failed_.insert(path);
        }
    });
}

void BackupStore::remove(const QString& path) {
    pool_.start([this, path] {
        QFile::remove(path);
        const QMutexLocker lock(&mutex_);
        failed_.remove(path);
    });
}

void BackupStore::waitForDone() {
    pool_.waitForDone();
}

bool BackupStore::failed(const QString& path) const {
    const QMutexLocker lock(&mutex_);
    return failed_.contains(path);
}

LoadedFile BackupStore::read(const QString& path) {
    return loadTextFile(path, Encoding::Utf8Bom);
}

} // namespace neko::pad
