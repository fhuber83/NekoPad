#pragma once

#include "core/text_file.hpp"

#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

namespace neko::pad {

/// Copies of unsaved documents -- new ones and modified files -- so that they survive
/// the application closing or crashing, as Notepad++'s session snapshot does. Each copy
/// is a file of its own in directory(): UTF-8 with a BOM and `\n` line breaks, which
/// holds any text exactly. The session names the file; what the document was (encoding,
/// line endings, its real path) is the session's to remember.
///
/// Writes and removals run on a thread of their own, one at a time, in the order they
/// were asked for: a removal never overtakes a write of the same file, and a later write
/// never loses to an earlier one. Each write is atomic (QSaveFile).
class BackupStore final {
public:
    explicit BackupStore(QString directory);
    /// Waits for what is queued.
    ~BackupStore();

    BackupStore(const BackupStore&) = delete;
    BackupStore& operator=(const BackupStore&) = delete;

    [[nodiscard]] QString directory() const { return directory_; }

    /// A file in directory() that no other backup uses, named after `name` ("new 1",
    /// "notes.txt") and the time. Nothing is written yet.
    [[nodiscard]] QString newPath(const QString& name);

    /// Writes `text` to `path` (one of newPath()'s), after whatever is queued.
    void write(const QString& path, QString text);
    /// Deletes `path`, after whatever is queued.
    void remove(const QString& path);

    /// Waits for everything queued.
    void waitForDone();
    /// Whether the latest write to `path` failed.
    [[nodiscard]] bool failed(const QString& path) const;

    /// Reads a backup (on any thread). A backup is always UTF-8.
    [[nodiscard]] static LoadedFile read(const QString& path);

private:
    QString directory_;
    QThreadPool pool_; ///< one thread: the queue's order is the order of the work
    QSet<QString> handedOut_;
    mutable QMutex mutex_;
    QSet<QString> failed_; ///< guarded by mutex_
};

} // namespace neko::pad
