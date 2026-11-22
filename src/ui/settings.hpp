#pragma once

#include "core/text_encoding.hpp"

#include <QDateTime>
#include <QFont>
#include <QString>
#include <QStringList>

namespace neko::pad::ui {

/// Neko Pad's settings, in QSettings() (organization "Neko", application "Neko Pad").
/// The theme and widget style are neko::theme::ThemeManager's ("ui/..." keys).
namespace settings {

[[nodiscard]] QFont editorFont(); ///< EditorDefaults::fallbackFont() until set
void setEditorFont(const QFont& font);

[[nodiscard]] bool lineWrap(); ///< off until set, as in Notepad++
void setLineWrap(bool enabled);

[[nodiscard]] bool showWhitespace();
void setShowWhitespace(bool show);
[[nodiscard]] bool showLineEndings();
void setShowLineEndings(bool show);

[[nodiscard]] bool restoreSession(); ///< on until set
void setRestoreSession(bool enabled);

/// Keep unsaved changes -- new documents and modified files -- across restarts instead of
/// asking about them on exit (Notepad++'s session snapshot); on until set. Needs
/// restoreSession().
[[nodiscard]] bool backupUnsaved();
void setBackupUnsaved(bool enabled);
/// Where the copies of unsaved documents go (BackupStore).
[[nodiscard]] QString backupDirectory();

[[nodiscard]] bool singleInstance(); ///< open files in the running window; on until set
void setSingleInstance(bool enabled);

[[nodiscard]] QStringList recentFiles();
void addRecentFile(const QString& path);
void clearRecentFiles();
inline constexpr int kMaxRecentFiles = 15;

[[nodiscard]] QString lastDirectory();
void setLastDirectory(const QString& directory);

[[nodiscard]] int zoom(); ///< points added to the editor font
void setZoom(int zoom);

/// One open document, for the session.
struct SessionFile {
    QString path; ///< empty for a new document
    int line = 0;
    int column = 0;
    QString languageId;   ///< empty: detect
    QString untitledName; ///< a new document's "new 1"
    /// The unsaved text, a BackupStore file; empty: none, the document is its file. With
    /// a backup, the session also keeps what the document is to be saved as.
    QString backup;
    Encoding encoding = Encoding::Utf8;
    Eol eol = nativeEol();
    QDateTime fileModified; ///< the file's time when the backup's changes began

    bool operator==(const SessionFile&) const = default;
};
[[nodiscard]] QList<SessionFile> sessionFiles();
[[nodiscard]] int sessionCurrentIndex();
void setSession(const QList<SessionFile>& files, int currentIndex);

[[nodiscard]] QByteArray windowGeometry();
[[nodiscard]] QByteArray windowState();
void setWindowGeometry(const QByteArray& geometry, const QByteArray& state);

} // namespace settings

/// Loads the stored editor font and line wrapping into neko::editor::EditorDefaults,
/// which every editor follows. Call before the first editor is built.
void applyStoredEditorSettings();

} // namespace neko::pad::ui
