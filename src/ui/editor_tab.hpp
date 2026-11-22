#pragma once

#include "core/language.hpp"
#include "core/text_encoding.hpp"
#include "core/text_file.hpp"

#include <QDateTime>
#include <QPointer>
#include <QSyntaxHighlighter>
#include <QUrl>
#include <QWidget>

#include <functional>
#include <optional>

namespace neko::editor {
class CodeEditor;
class EditorFindBar;
} // namespace neko::editor

namespace neko::pad::ui {

/// One open document: a neko::editor::CodeEditor with its Find bar, and what the file
/// it came from was like -- path, encoding, line endings, language.
///
/// Loading runs on a worker thread (loadTextFile: read, detect, decode, normalize); only
/// handing the text to the editor happens on the UI thread. A tab can also wait with
/// its file until it is first shown (setPendingFile), so a restored session with many
/// files opens as fast as one with a single file.
///
/// Large texts stream into the editor in slices from the event loop (the first screen
/// shows at once); files larger than kHighlightLimit are shown without highlighting.
class EditorTab final : public QWidget {
    Q_OBJECT

public:
    /// Larger files are shown without syntax highlighting.
    static constexpr qint64 kHighlightLimit = 32LL * 1024 * 1024;
    /// Texts longer than this (in characters) stream into the editor in slices.
    static constexpr qsizetype kStreamThreshold = 1024 * 1024;
    static constexpr qsizetype kFirstSlice = 64 * 1024;
    static constexpr qsizetype kSlice = 512 * 1024;

    enum class LoadState {
        Ready,   ///< has its text (or is a new document)
        Pending, ///< knows its file, has not started reading it
        Loading, ///< a worker reads it
        Failed,  ///< reading it failed; error() says why
    };

    explicit EditorTab(QWidget* parent = nullptr);
    ~EditorTab() override;

    [[nodiscard]] neko::editor::CodeEditor* editor() const noexcept { return editor_; }
    [[nodiscard]] neko::editor::EditorFindBar* findBar() const noexcept { return findBar_; }

    // ---- The file ----------------------------------------------------------------------

    [[nodiscard]] QString filePath() const { return path_; }
    [[nodiscard]] bool isUntitled() const noexcept { return path_.isEmpty(); }
    /// The file name, or the untitled name ("new 1").
    [[nodiscard]] QString displayName() const;
    void setUntitledName(const QString& name);
    [[nodiscard]] QString untitledName() const { return untitledName_; }

    [[nodiscard]] LoadState loadState() const noexcept { return state_; }
    [[nodiscard]] QString error() const { return error_; }

    /// Remembers `path` and reads it the first time ensureLoaded() is called.
    void setPendingFile(const QString& path);
    /// Starts reading `path` on a worker thread; loaded() follows. `forced` decodes it in
    /// that encoding instead of the detected one.
    void load(const QString& path, std::optional<Encoding> forced = std::nullopt);
    /// Reads the file now if it is pending; nothing otherwise.
    void ensureLoaded();
    /// Reads the file again (dropping unsaved changes), in `forced` if given.
    void reload(std::optional<Encoding> forced = std::nullopt);
    /// The synchronous half of load(), for a file already read.
    void applyLoaded(const QString& path, const LoadedFile& file);

    /// Writes the document to filePath(), or to `path` (Save As: the tab takes it).
    [[nodiscard]] SaveResult save();
    [[nodiscard]] SaveResult saveAs(const QString& path);

    /// The file's modification time when it was last read or written here; the window
    /// compares it to tell our own writes from other programs'.
    [[nodiscard]] QDateTime knownModified() const { return knownModified_; }
    /// Accepts the file as it is on disk now (the user declined to reload it).
    void setKnownModified(const QDateTime& modified) { knownModified_ = modified; }

    // ---- What the document is ----------------------------------------------------------

    [[nodiscard]] Encoding encoding() const noexcept { return encoding_; }
    /// Saves in `encoding` from now on; marks the document modified.
    void setEncoding(Encoding encoding);
    [[nodiscard]] Eol eol() const noexcept { return eol_; }
    /// Saves with `eol` line endings from now on; marks the document modified.
    void setEol(Eol eol);
    [[nodiscard]] bool hadMixedEols() const noexcept { return mixedEols_; }
    [[nodiscard]] bool looksBinary() const noexcept { return looksBinary_; }
    [[nodiscard]] qint64 fileSize() const noexcept { return fileSize_; }

    [[nodiscard]] const Language& language() const noexcept { return *language_; }
    /// Highlights as `language` (the user chose it: languageChosen() is then true).
    void setLanguage(const Language& language);
    [[nodiscard]] bool languageChosen() const noexcept { return languageChosen_; }
    /// Highlighting is off because the file is too large.
    [[nodiscard]] bool highlightingSuppressed() const noexcept { return highlightSuppressed_; }
    [[nodiscard]] QSyntaxHighlighter* highlighter() const noexcept { return highlighter_; }

    [[nodiscard]] bool isModified() const;
    [[nodiscard]] bool isReadOnly() const;
    void setReadOnly(bool readOnly);

    void setDark(bool dark);
    void setShowWhitespace(bool spaces, bool lineEndings);
    /// The editor font is EditorDefaults' plus `points`.
    void setZoom(int points);

    // ---- Cursor ------------------------------------------------------------------------

    /// Zero-based line and column (characters into the line).
    [[nodiscard]] std::pair<int, int> cursorLineColumn() const;
    void goToLine(int line, int column = 0);
    /// cursorLineColumn(), or for a tab whose file is not read yet, where its cursor
    /// will go -- what a session stores.
    [[nodiscard]] std::pair<int, int> sessionCursor() const;

    // ---- Line operations (Edit menu); each one undo step ---------------------------------

    /// Replaces the lines the selection touches (or the cursor's line) with what `op` makes
    /// of them, and selects the result.
    void transformSelectedLines(const std::function<QStringList(QStringList)>& op);
    /// Replaces the selection (or the whole document if there is none) with `op` of it.
    void transformSelection(const std::function<QString(const QString&)>& op);
    void duplicateLines();
    void deleteLines();
    void moveLines(bool up);
    void toggleComment();

signals:
    /// The text arrived (or failed to: loadState() says which).
    void loaded();
    /// Path, encoding, line endings, language, read-only or modified state changed: the
    /// tab title and status bar should look again.
    void infoChanged();
    void filesDropped(const QList<QUrl>& urls);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installHighlighter();
    void appendSlice(quint64 generation);
    void finishLoading();
    /// The first and last line the selection touches; a selection ending at the start of
    /// a line does not touch it.
    [[nodiscard]] std::pair<int, int> selectedLineRange() const;

    neko::editor::CodeEditor* editor_ = nullptr;
    neko::editor::EditorFindBar* findBar_ = nullptr;
    QPointer<QSyntaxHighlighter> highlighter_;

    QString path_;
    QString untitledName_;
    LoadState state_ = LoadState::Ready;
    QString error_;
    QDateTime knownModified_;
    std::optional<std::pair<int, int>> pendingCursor_;
    quint64 loadGeneration_ = 0; ///< ignores the result of a load that a newer one replaced
    QString streamText_;         ///< the text still streaming into the editor
    qsizetype streamPos_ = 0;
    bool userReadOnly_ = false; ///< read-only as the user set it, while loading forces it

    Encoding encoding_ = Encoding::Utf8;
    Eol eol_ = nativeEol();
    bool mixedEols_ = false;
    bool looksBinary_ = false;
    qint64 fileSize_ = 0;
    const Language* language_;
    bool languageChosen_ = false;
    bool highlightSuppressed_ = false;
    bool dark_ = false;
    int zoom_ = 0;
};

} // namespace neko::pad::ui
