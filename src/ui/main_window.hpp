#pragma once

#include "core/text_encoding.hpp"

#include <QHash>
#include <QMainWindow>
#include <QSet>

class QAction;
class QActionGroup;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QTabWidget;
class QTimer;
class QToolButton;

namespace neko::theme {
class ResponsiveToolBar;
}

namespace neko::pad::ui {

class EditorTab;

/// The Neko Pad window: tabs of EditorTab, the menus and tool bar of a Notepad++-style
/// editor, and the status bar (position, selection, length, line endings, encoding,
/// language, insert / overwrite).
///
/// Files open on worker threads and appear as soon as they are read; tabs of a
/// restored session read their file only when they are first shown. Files changed by
/// other programs are noticed and offered for reloading.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Opens each file (or switches to it if it is open already); asynchronous. The
    /// last one becomes the current tab. A `path:line` form jumps to that line.
    void openFiles(const QStringList& paths);
    /// The tab showing `path`, or nullptr.
    [[nodiscard]] EditorTab* tabForPath(const QString& path) const;

    /// Opens the files of the last session (lazily: only the current one is read).
    void restoreSession();
    void saveSession() const;

    /// A new, empty "new N" document.
    EditorTab* newDocument();

    [[nodiscard]] EditorTab* currentTab() const;
    [[nodiscard]] EditorTab* tabAt(int index) const;
    [[nodiscard]] int tabCount() const;
    [[nodiscard]] QTabWidget* tabWidget() const noexcept { return tabs_; }

    /// Saves `tab` (asking for a name if it has none). False if it was not saved.
    bool saveTab(EditorTab* tab);
    bool saveTabAs(EditorTab* tab);
    /// Closes `tab`, asking about unsaved changes unless `force`. False if the user
    /// cancelled.
    bool closeTab(EditorTab* tab, bool force = false);

    /// The status bar's texts, for tests.
    [[nodiscard]] QString statusPositionText() const;
    [[nodiscard]] QString statusEncodingText() const;
    [[nodiscard]] QString statusEolText() const;
    [[nodiscard]] QString statusLanguageText() const;

    /// The action with object name `name` ("action-save", ...), for tests.
    [[nodiscard]] QAction* action(const QString& name) const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();

    QAction* makeAction(
        const QString& name, const QString& text, const QString& icon = {}, const QList<QKeySequence>& shortcuts = {});

    EditorTab* createTab();
    void addTab(EditorTab* tab, bool makeCurrent);
    void connectTab(EditorTab* tab);
    void currentTabChanged();
    void updateTabTitle(EditorTab* tab);
    void updateWindowTitle();
    void updateStatus();
    void updatePosition();
    void updateActions();
    void tabLoaded(EditorTab* tab);

    void openFileDialog();
    void reopenClosedTab();
    void closeOtherTabs(EditorTab* keep);
    void closeTabsToTheRight(EditorTab* tab);
    void closeAllTabs();
    bool saveAll();
    void reloadCurrent(std::optional<Encoding> forced);
    void showTabContextMenu(const QPoint& pos);
    void goToLineDialog();
    void chooseFont();
    void setZoom(int zoom);
    void showAbout();

    void watch(EditorTab* tab);
    void unwatch(const QString& path);
    void fileChangedOnDisk(const QString& path);
    void checkChangedFiles();

    void rebuildRecentMenu();
    void rebuildLanguageMenu();
    void syncLanguageMenu();
    void syncEncodingMenus();
    [[nodiscard]] QString nextUntitledName();

    QTabWidget* tabs_ = nullptr;
    neko::theme::ResponsiveToolBar* toolBar_ = nullptr;
    QHash<QString, QAction*> actions_;

    QMenu* recentMenu_ = nullptr;
    QMenu* languageMenu_ = nullptr;
    QMenu* encodingMenu_ = nullptr;
    QMenu* reloadEncodingMenu_ = nullptr;
    QMenu* eolMenu_ = nullptr;
    QActionGroup* languageGroup_ = nullptr;
    QActionGroup* encodingGroup_ = nullptr;
    QActionGroup* eolGroup_ = nullptr;

    QLabel* positionLabel_ = nullptr;
    QLabel* lengthLabel_ = nullptr;
    QToolButton* eolButton_ = nullptr;
    QToolButton* encodingButton_ = nullptr;
    QToolButton* languageButton_ = nullptr;
    QLabel* modeLabel_ = nullptr;

    QFileSystemWatcher* watcher_ = nullptr;
    QSet<QString> changedOnDisk_; ///< paths waiting to be offered for reloading
    QTimer* changeTimer_ = nullptr;
    bool checkingChanges_ = false;

    QStringList closedPaths_; ///< for Reopen Closed Tab, latest last
    int zoom_ = 0;
    bool closing_ = false;
};

} // namespace neko::pad::ui
