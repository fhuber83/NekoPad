#include "ui/settings.hpp"

#include "neko/editor/editor_defaults.hpp"

#include <QDir>
#include <QSettings>

namespace neko::pad::ui {

namespace settings {

namespace {

constexpr auto kFont = "editor/font";
constexpr auto kLineWrap = "editor/line_wrap";
constexpr auto kShowWhitespace = "editor/show_whitespace";
constexpr auto kShowLineEndings = "editor/show_line_endings";
constexpr auto kZoom = "editor/zoom";
constexpr auto kRestoreSession = "general/restore_session";
constexpr auto kSingleInstance = "general/single_instance";
constexpr auto kRecentFiles = "general/recent_files";
constexpr auto kLastDirectory = "general/last_directory";
constexpr auto kGeometry = "window/geometry";
constexpr auto kState = "window/state";

[[nodiscard]] QString key(const char* k) {
    return QString::fromLatin1(k);
}

} // namespace

QFont editorFont() {
    QSettings s;
    QFont font = neko::editor::EditorDefaults::fallbackFont();
    if (const QString stored = s.value(key(kFont)).toString(); !stored.isEmpty()) {
        QFont candidate;
        if (candidate.fromString(stored)) {
            font = candidate;
        }
    }
    return font;
}

void setEditorFont(const QFont& font) {
    QSettings().setValue(key(kFont), font.toString());
}

bool lineWrap() {
    return QSettings().value(key(kLineWrap), false).toBool();
}
void setLineWrap(bool enabled) {
    QSettings().setValue(key(kLineWrap), enabled);
}

bool showWhitespace() {
    return QSettings().value(key(kShowWhitespace), false).toBool();
}
void setShowWhitespace(bool show) {
    QSettings().setValue(key(kShowWhitespace), show);
}
bool showLineEndings() {
    return QSettings().value(key(kShowLineEndings), false).toBool();
}
void setShowLineEndings(bool show) {
    QSettings().setValue(key(kShowLineEndings), show);
}

bool restoreSession() {
    return QSettings().value(key(kRestoreSession), true).toBool();
}
void setRestoreSession(bool enabled) {
    QSettings().setValue(key(kRestoreSession), enabled);
}

bool singleInstance() {
    return QSettings().value(key(kSingleInstance), true).toBool();
}
void setSingleInstance(bool enabled) {
    QSettings().setValue(key(kSingleInstance), enabled);
}

QStringList recentFiles() {
    return QSettings().value(key(kRecentFiles)).toStringList();
}

void addRecentFile(const QString& path) {
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecentFiles) {
        files.removeLast();
    }
    QSettings().setValue(key(kRecentFiles), files);
}

void clearRecentFiles() {
    QSettings().remove(key(kRecentFiles));
}

QString lastDirectory() {
    return QSettings().value(key(kLastDirectory), QDir::homePath()).toString();
}
void setLastDirectory(const QString& directory) {
    QSettings().setValue(key(kLastDirectory), directory);
}

int zoom() {
    return QSettings().value(key(kZoom), 0).toInt();
}
void setZoom(int zoom) {
    QSettings().setValue(key(kZoom), zoom);
}

QList<SessionFile> sessionFiles() {
    QSettings s;
    QList<SessionFile> files;
    const int n = s.beginReadArray(QStringLiteral("session/files"));
    files.reserve(n);
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        SessionFile f;
        f.path = s.value(QStringLiteral("path")).toString();
        f.line = s.value(QStringLiteral("line")).toInt();
        f.column = s.value(QStringLiteral("column")).toInt();
        f.languageId = s.value(QStringLiteral("language")).toString();
        if (!f.path.isEmpty()) {
            files << f;
        }
    }
    s.endArray();
    return files;
}

int sessionCurrentIndex() {
    return QSettings().value(QStringLiteral("session/current"), 0).toInt();
}

void setSession(const QList<SessionFile>& files, int currentIndex) {
    QSettings s;
    s.remove(QStringLiteral("session/files"));
    s.beginWriteArray(QStringLiteral("session/files"), static_cast<int>(files.size()));
    for (int i = 0; i < files.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("path"), files[i].path);
        s.setValue(QStringLiteral("line"), files[i].line);
        s.setValue(QStringLiteral("column"), files[i].column);
        if (!files[i].languageId.isEmpty()) {
            s.setValue(QStringLiteral("language"), files[i].languageId);
        }
    }
    s.endArray();
    s.setValue(QStringLiteral("session/current"), currentIndex);
}

QByteArray windowGeometry() {
    return QSettings().value(key(kGeometry)).toByteArray();
}
QByteArray windowState() {
    return QSettings().value(key(kState)).toByteArray();
}

void setWindowGeometry(const QByteArray& geometry, const QByteArray& state) {
    QSettings s;
    s.setValue(key(kGeometry), geometry);
    s.setValue(key(kState), state);
}

} // namespace settings

void applyStoredEditorSettings() {
    auto& defaults = neko::editor::EditorDefaults::instance();
    defaults.setFont(settings::editorFont());
    defaults.setLineWrap(settings::lineWrap());
}

} // namespace neko::pad::ui
