#include "ui/main_window.hpp"

#include "core/backup_store.hpp"
#include "core/language.hpp"
#include "core/line_ops.hpp"
#include "core/text_file.hpp"
#include "ui/app_icon.hpp"
#include "ui/editor_tab.hpp"
#include "ui/settings.hpp"

#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_defaults.hpp"
#include "neko/editor/editor_find_bar.hpp"
#include "neko/theme/responsive_tool_bar.hpp"
#include "neko/theme/theme_manager.hpp"
#include "neko/theme/theme_menus.hpp"
#include "neko/theme/themed_icon.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

namespace neko::pad::ui {

namespace ed = neko::editor;
namespace th = neko::theme;

namespace {

constexpr qint64 kAskBeforeOpening = 512LL * 1024 * 1024;

[[nodiscard]] Qt::CaseSensitivity pathCaseSensitivity() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

/// The path to compare and show: canonical where the file exists (symlinks resolved),
/// else absolute and clean.
[[nodiscard]] QString normalizedPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

[[nodiscard]] bool samePath(const QString& a, const QString& b) {
    return QString::compare(a, b, pathCaseSensitivity()) == 0;
}

/// "file.txt:42" -> ("file.txt", 42) when "file.txt:42" itself is not a file.
[[nodiscard]] std::pair<QString, int> splitLineSuffix(const QString& arg) {
    if (QFileInfo::exists(arg)) {
        return {arg, 0};
    }
    static const QRegularExpression re(QStringLiteral("^(.+):(\\d+)(?::\\d+)?$"));
    if (const QRegularExpressionMatch m = re.match(arg); m.hasMatch()) {
        return {m.captured(1), m.captured(2).toInt()};
    }
    return {arg, 0};
}

} // namespace

MainWindow::MainWindow(QWidget* parent, const QString& backupDirectory)
    : QMainWindow(parent),
      backups_(
          std::make_unique<BackupStore>(backupDirectory.isEmpty() ? settings::backupDirectory() : backupDirectory)) {
    setWindowIcon(appIcon());
    setAcceptDrops(true);
    zoom_ = settings::zoom();

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setUsesScrollButtons(true);
    tabs_->tabBar()->setElideMode(Qt::ElideNone);
    tabs_->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    tabs_->tabBar()->installEventFilter(this);
    setCentralWidget(tabs_);

    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::currentTabChanged);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (EditorTab* tab = tabAt(index)) {
            closeTab(tab);
        }
    });
    connect(tabs_, &QTabWidget::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0) {
            newDocument(); // double-click on the empty part of the tab bar
        }
    });
    connect(tabs_->tabBar(), &QWidget::customContextMenuRequested, this, &MainWindow::showTabContextMenu);

    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, &MainWindow::fileChangedOnDisk);
    changeTimer_ = new QTimer(this);
    changeTimer_->setSingleShot(true);
    changeTimer_->setInterval(250);
    connect(changeTimer_, &QTimer::timeout, this, &MainWindow::checkChangedFiles);
    backupTimer_ = new QTimer(this);
    backupTimer_->setInterval(kBackupIntervalMs);
    connect(backupTimer_, &QTimer::timeout, this, &MainWindow::backupNow);
    backupTimer_->start();

    buildActions();
    buildMenus();
    buildToolBar();
    buildStatusBar();

    connect(&th::ThemeManager::instance(), &th::ThemeManager::themeChanged, this, [this](th::ThemeMode, bool dark) {
        for (int i = 0; i < tabCount(); ++i) {
            tabAt(i)->setDark(dark);
        }
    });

    resize(1100, 750);
    if (const QByteArray geometry = settings::windowGeometry(); !geometry.isEmpty()) {
        restoreGeometry(geometry);
        restoreState(settings::windowState());
    }
    updateWindowTitle();
    updateStatus();
}

MainWindow::~MainWindow() = default;

// ---- Actions and menus ------------------------------------------------------------------

QAction* MainWindow::makeAction(
    const QString& name, const QString& text, const QString& icon, const QList<QKeySequence>& shortcuts) {
    auto* action = new QAction(text, this);
    action->setObjectName(QStringLiteral("action-") + name);
    if (!icon.isEmpty()) {
        action->setIcon(th::themedIcon(icon));
    }
    if (!shortcuts.isEmpty()) {
        action->setShortcuts(shortcuts);
    }
    addAction(action); // shortcuts work while the menu bar is hidden, too
    actions_.insert(name, action);
    return action;
}

QAction* MainWindow::action(const QString& name) const {
    return actions_.value(name);
}

void MainWindow::buildActions() {
    // Runs `f` on the current tab, if there is one.
    auto onTab = [this](auto f) {
        return [this, f] {
            if (EditorTab* tab = currentTab()) {
                f(tab);
            }
        };
    };
    // A bulk line operation: on the selected lines, or on the whole document.
    auto onLines = [this](std::function<QStringList(QStringList)> op) {
        return [this, op = std::move(op)] {
            if (EditorTab* tab = currentTab()) {
                if (!tab->editor()->textCursor().hasSelection()) {
                    tab->editor()->selectAll();
                }
                tab->transformSelectedLines(op);
            }
        };
    };

    // File
    connect(
        makeAction(QStringLiteral("new"), tr("&New"), QStringLiteral("new"), {QKeySequence::New}),
        &QAction::triggered,
        this,
        [this] { newDocument(); });
    connect(
        makeAction(QStringLiteral("open"), tr("&Open..."), QStringLiteral("open"), {QKeySequence::Open}),
        &QAction::triggered,
        this,
        &MainWindow::openFileDialog);
    connect(
        makeAction(QStringLiteral("reload"), tr("Re&load from Disk"), QStringLiteral("reload")),
        &QAction::triggered,
        this,
        [this] { reloadCurrent(std::nullopt); });
    connect(
        makeAction(QStringLiteral("save"), tr("&Save"), QStringLiteral("save"), {QKeySequence::Save}),
        &QAction::triggered,
        this,
        onTab([this](EditorTab* t) { saveTab(t); }));
    connect(
        makeAction(QStringLiteral("save-as"), tr("Save &As..."), {}, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)}),
        &QAction::triggered,
        this,
        onTab([this](EditorTab* t) { saveTabAs(t); }));
    connect(
        makeAction(
            QStringLiteral("save-all"),
            tr("Sa&ve All"),
            QStringLiteral("save-all"),
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}),
        &QAction::triggered,
        this,
        [this] { saveAll(); });
    connect(
        makeAction(QStringLiteral("close"), tr("&Close"), QStringLiteral("tab-close"), {QKeySequence::Close}),
        &QAction::triggered,
        this,
        onTab([this](EditorTab* t) { closeTab(t); }));
    connect(
        makeAction(QStringLiteral("close-all"), tr("Clos&e All"), {}, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W)}),
        &QAction::triggered,
        this,
        &MainWindow::closeAllTabs);
    connect(
        makeAction(QStringLiteral("close-others"), tr("Close All &But This")),
        &QAction::triggered,
        this,
        onTab([this](EditorTab* t) { closeOtherTabs(t); }));
    connect(
        makeAction(
            QStringLiteral("reopen-closed"),
            tr("Reopen Closed &Tab"),
            {},
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T)}),
        &QAction::triggered,
        this,
        &MainWindow::reopenClosedTab);
    connect(
        makeAction(QStringLiteral("open-folder"), tr("Open Containing &Folder"), QStringLiteral("folder")),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) {
            if (t->isUntitled()) {
                return;
            }
            const QString path = QDir::toNativeSeparators(t->filePath());
#if defined(Q_OS_WIN)
            QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), path});
#elif defined(Q_OS_MACOS)
            QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(t->filePath()).absolutePath()));
#endif
        }));
    connect(
        makeAction(QStringLiteral("quit"), tr("E&xit"), QStringLiteral("quit"), {QKeySequence::Quit}),
        &QAction::triggered,
        this,
        &QWidget::close);

    // Edit
    connect(
        makeAction(QStringLiteral("undo"), tr("&Undo"), QStringLiteral("undo"), {QKeySequence::Undo}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->editor()->undo(); }));
    connect(
        makeAction(QStringLiteral("redo"), tr("&Redo"), QStringLiteral("redo"), {QKeySequence::Redo}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->editor()->redo(); }));
    connect(
        makeAction(QStringLiteral("cut"), tr("Cu&t"), QStringLiteral("cut"), {QKeySequence::Cut}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->editor()->hasBlockSelection() ? t->editor()->cutBlock() : t->editor()->cut(); }));
    connect(
        makeAction(QStringLiteral("copy"), tr("&Copy"), QStringLiteral("copy"), {QKeySequence::Copy}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->editor()->hasBlockSelection() ? t->editor()->copyBlock() : t->editor()->copy(); }));
    connect(
        makeAction(QStringLiteral("paste"), tr("&Paste"), QStringLiteral("paste"), {QKeySequence::Paste}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->editor()->paste(); }));
    connect(
        makeAction(QStringLiteral("select-all"), tr("Select A&ll"), {}, {QKeySequence::SelectAll}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->editor()->selectAll(); }));
    connect(
        makeAction(QStringLiteral("duplicate"), tr("&Duplicate Line"), {}, {QKeySequence(Qt::CTRL | Qt::Key_D)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->duplicateLines(); }));
    connect(
        makeAction(
            QStringLiteral("delete-line"), tr("D&elete Line"), {}, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->deleteLines(); }));
    connect(
        makeAction(
            QStringLiteral("move-up"), tr("Move Line &Up"), {}, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->moveLines(true); }));
    connect(
        makeAction(
            QStringLiteral("move-down"),
            tr("Move Line Do&wn"),
            {},
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->moveLines(false); }));
    connect(
        makeAction(QStringLiteral("join-lines"), tr("&Join Lines"), {}, {QKeySequence(Qt::CTRL | Qt::Key_J)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) {
            t->transformSelectedLines([](const QStringList& l) { return QStringList{lines::joined(l)}; });
        }));
    QList<QKeySequence> commentKeys{QKeySequence(Qt::CTRL | Qt::Key_Slash)};
#ifdef Q_OS_WIN
    commentKeys << QKeySequence(Qt::CTRL | Qt::Key_Q); // Notepad++'s; Quit elsewhere
#endif
    connect(
        makeAction(QStringLiteral("toggle-comment"), tr("Toggle &Comment"), QStringLiteral("comment"), commentKeys),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->toggleComment(); }));
    connect(
        makeAction(
            QStringLiteral("upper-case"), tr("&UPPER CASE"), {}, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->transformSelection([](const QString& s) { return s.toUpper(); }); }));
    connect(
        makeAction(QStringLiteral("lower-case"), tr("&lower case"), {}, {QKeySequence(Qt::CTRL | Qt::Key_U)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->transformSelection([](const QString& s) { return s.toLower(); }); }));
    connect(
        makeAction(QStringLiteral("sort-asc"), tr("Sort Lines &Ascending")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::sorted(std::move(l), true, Qt::CaseSensitive); }));
    connect(
        makeAction(QStringLiteral("sort-desc"), tr("Sort Lines &Descending")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::sorted(std::move(l), false, Qt::CaseSensitive); }));
    connect(
        makeAction(QStringLiteral("sort-asc-ci"), tr("Sort Lines Ascending, &Ignoring Case")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::sorted(std::move(l), true, Qt::CaseInsensitive); }));
    connect(
        makeAction(QStringLiteral("remove-duplicates"), tr("Remove D&uplicate Lines")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::withoutDuplicates(l); }));
    connect(
        makeAction(QStringLiteral("remove-empty"), tr("Remove &Empty Lines")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::withoutEmpty(l, false); }));
    connect(
        makeAction(QStringLiteral("remove-blank"), tr("Remove Empty Lines (Including &Blank)")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::withoutEmpty(l, true); }));
    connect(
        makeAction(QStringLiteral("trim-trailing"), tr("Trim &Trailing Space")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::trimmedTrailing(std::move(l)); }));
    connect(
        makeAction(QStringLiteral("trim-leading"), tr("Trim &Leading Space")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::trimmedLeading(std::move(l)); }));
    connect(
        makeAction(QStringLiteral("tabs-to-spaces"), tr("Leading TAB to &Space")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::indentationToSpaces(std::move(l), ed::CodeEditor::kTabWidth); }));
    connect(
        makeAction(QStringLiteral("spaces-to-tabs"), tr("Leading Space to TA&B")),
        &QAction::triggered,
        this,
        onLines([](QStringList l) { return lines::indentationToTabs(std::move(l), ed::CodeEditor::kTabWidth); }));
    QAction* readOnly = makeAction(QStringLiteral("read-only"), tr("&Read-Only"), QStringLiteral("read-only"));
    readOnly->setCheckable(true);
    connect(readOnly, &QAction::triggered, this, [this](bool on) {
        if (EditorTab* t = currentTab()) {
            t->setReadOnly(on);
        }
    });
    connect(
        makeAction(QStringLiteral("copy-path"), tr("Copy Full &Path")),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) {
            if (!t->isUntitled()) {
                QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(t->filePath()));
            }
        }));
    connect(
        makeAction(QStringLiteral("copy-name"), tr("Copy File &Name")),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { QGuiApplication::clipboard()->setText(t->displayName()); }));

    // Search
    connect(
        makeAction(QStringLiteral("find"), tr("&Find..."), QStringLiteral("find"), {QKeySequence::Find}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->findBar()->openFind(); }));
    connect(
        makeAction(
            QStringLiteral("replace"),
            tr("&Replace..."),
            QStringLiteral("replace"),
            {QKeySequence(Qt::CTRL | Qt::Key_H)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) { t->findBar()->openReplace(); }));
    connect(
        makeAction(QStringLiteral("find-next"), tr("Find &Next"), {}, {QKeySequence(Qt::Key_F3)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) {
            if (t->findBar()->findField()->text().isEmpty()) {
                t->findBar()->openFind();
            } else {
                t->findBar()->findNext(true);
            }
        }));
    connect(
        makeAction(QStringLiteral("find-previous"), tr("Find &Previous"), {}, {QKeySequence(Qt::SHIFT | Qt::Key_F3)}),
        &QAction::triggered,
        this,
        onTab([](EditorTab* t) {
            if (t->findBar()->findField()->text().isEmpty()) {
                t->findBar()->openFind();
            } else {
                t->findBar()->findNext(false);
            }
        }));
    connect(
        makeAction(
            QStringLiteral("go-to-line"),
            tr("&Go to Line..."),
            QStringLiteral("go-to-line"),
            {QKeySequence(Qt::CTRL | Qt::Key_G)}),
        &QAction::triggered,
        this,
        &MainWindow::goToLineDialog);

    // View
    QAction* wrap = makeAction(QStringLiteral("word-wrap"), tr("&Word Wrap"), QStringLiteral("line-wrap"));
    wrap->setCheckable(true);
    wrap->setChecked(ed::EditorDefaults::instance().lineWrap());
    wrap->setToolTip(tr("Wrap long lines in every document (Alt+Z: this document only)"));
    connect(wrap, &QAction::triggered, this, [](bool on) {
        settings::setLineWrap(on);
        ed::EditorDefaults::instance().setLineWrap(on);
    });
    QAction* spaces =
        makeAction(QStringLiteral("show-whitespace"), tr("Show &Whitespace"), QStringLiteral("show-whitespace"));
    spaces->setCheckable(true);
    spaces->setChecked(settings::showWhitespace());
    QAction* eols = makeAction(QStringLiteral("show-eol"), tr("Show &End of Line"));
    eols->setCheckable(true);
    eols->setChecked(settings::showLineEndings());
    auto applyWhitespace = [this, spaces, eols] {
        settings::setShowWhitespace(spaces->isChecked());
        settings::setShowLineEndings(eols->isChecked());
        for (int i = 0; i < tabCount(); ++i) {
            tabAt(i)->setShowWhitespace(spaces->isChecked(), eols->isChecked());
        }
    };
    connect(spaces, &QAction::triggered, this, applyWhitespace);
    connect(eols, &QAction::triggered, this, applyWhitespace);
    connect(
        makeAction(
            QStringLiteral("zoom-in"),
            tr("Zoom &In"),
            QStringLiteral("zoom-in"),
            {QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)}),
        &QAction::triggered,
        this,
        [this] { setZoom(zoom_ + 1); });
    connect(
        makeAction(QStringLiteral("zoom-out"), tr("Zoom &Out"), QStringLiteral("zoom-out"), {QKeySequence::ZoomOut}),
        &QAction::triggered,
        this,
        [this] { setZoom(zoom_ - 1); });
    connect(
        makeAction(QStringLiteral("zoom-reset"), tr("&Restore Default Zoom"), {}, {QKeySequence(Qt::CTRL | Qt::Key_0)}),
        &QAction::triggered,
        this,
        [this] { setZoom(0); });
    QAction* fullScreen = makeAction(
        QStringLiteral("full-screen"), tr("&Full Screen"), {}, {QKeySequence::FullScreen, QKeySequence(Qt::Key_F11)});
    fullScreen->setCheckable(true);
    connect(fullScreen, &QAction::triggered, this, [this](bool on) { on ? showFullScreen() : showNormal(); });
    connect(
        makeAction(
            QStringLiteral("next-tab"),
            tr("Ne&xt Tab"),
            {},
            {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)}),
        &QAction::triggered,
        this,
        [this] {
            if (tabCount() > 1) {
                tabs_->setCurrentIndex((tabs_->currentIndex() + 1) % tabCount());
            }
        });
    connect(
        makeAction(
            QStringLiteral("previous-tab"),
            tr("Pre&vious Tab"),
            {},
            {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageUp)}),
        &QAction::triggered,
        this,
        [this] {
            if (tabCount() > 1) {
                tabs_->setCurrentIndex((tabs_->currentIndex() + tabCount() - 1) % tabCount());
            }
        });

    // Settings
    connect(
        makeAction(QStringLiteral("font"), tr("&Font..."), QStringLiteral("font")),
        &QAction::triggered,
        this,
        &MainWindow::chooseFont);
    QAction* session = makeAction(QStringLiteral("restore-session"), tr("&Remember Open Files"));
    session->setCheckable(true);
    session->setChecked(settings::restoreSession());
    QAction* backup = makeAction(QStringLiteral("backup-unsaved"), tr("Remember &Unsaved Changes"));
    backup->setCheckable(true);
    backup->setChecked(settings::backupUnsaved());
    backup->setEnabled(settings::restoreSession());
    backup->setToolTip(tr("Keep new documents and unsaved changes when Neko Pad closes, without asking"));
    connect(backup, &QAction::triggered, this, [](bool on) { settings::setBackupUnsaved(on); });
    connect(session, &QAction::triggered, this, [backup](bool on) {
        settings::setRestoreSession(on);
        backup->setEnabled(on);
    });
    QAction* single = makeAction(QStringLiteral("single-instance"), tr("Open Files in the &Running Window"));
    single->setCheckable(true);
    single->setChecked(settings::singleInstance());
    connect(single, &QAction::triggered, this, [](bool on) { settings::setSingleInstance(on); });

    // Help
    connect(
        makeAction(QStringLiteral("about"), tr("&About Neko Pad"), QStringLiteral("about")),
        &QAction::triggered,
        this,
        &MainWindow::showAbout);
    connect(makeAction(QStringLiteral("about-qt"), tr("About &Qt")), &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::buildMenus() {
    QMenuBar* bar = menuBar();
    auto a = [this](const char* name) {
        return action(QString::fromLatin1(name));
    };

    QMenu* file = bar->addMenu(tr("&File"));
    file->addAction(a("new"));
    file->addAction(a("open"));
    recentMenu_ = file->addMenu(th::themedIcon(QStringLiteral("recent")), tr("Recent &Files"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addAction(a("reopen-closed"));
    file->addAction(a("reload"));
    file->addSeparator();
    file->addAction(a("save"));
    file->addAction(a("save-as"));
    file->addAction(a("save-all"));
    file->addSeparator();
    file->addAction(a("close"));
    file->addAction(a("close-all"));
    file->addAction(a("close-others"));
    file->addSeparator();
    file->addAction(a("open-folder"));
    file->addSeparator();
    file->addAction(a("quit"));

    QMenu* edit = bar->addMenu(tr("&Edit"));
    edit->addAction(a("undo"));
    edit->addAction(a("redo"));
    edit->addSeparator();
    edit->addAction(a("cut"));
    edit->addAction(a("copy"));
    edit->addAction(a("paste"));
    edit->addAction(a("select-all"));
    edit->addSeparator();
    QMenu* lineOps = edit->addMenu(tr("&Line Operations"));
    for (const char* name : {"duplicate", "delete-line", "move-up", "move-down", "join-lines"}) {
        lineOps->addAction(a(name));
    }
    lineOps->addSeparator();
    for (const char* name : {"sort-asc", "sort-desc", "sort-asc-ci"}) {
        lineOps->addAction(a(name));
    }
    lineOps->addSeparator();
    for (const char* name : {"remove-duplicates", "remove-empty", "remove-blank"}) {
        lineOps->addAction(a(name));
    }
    QMenu* caseMenu = edit->addMenu(tr("Convert C&ase To"));
    caseMenu->addAction(a("upper-case"));
    caseMenu->addAction(a("lower-case"));
    QMenu* blank = edit->addMenu(tr("&Blank Operations"));
    for (const char* name : {"trim-trailing", "trim-leading", "tabs-to-spaces", "spaces-to-tabs"}) {
        blank->addAction(a(name));
    }
    edit->addAction(a("toggle-comment"));
    edit->addSeparator();
    eolMenu_ = edit->addMenu(tr("EOL Con&version"));
    eolGroup_ = new QActionGroup(this);
    for (const Eol eol : kAllEols) {
        QAction* act = eolMenu_->addAction(eolName(eol));
        act->setCheckable(true);
        act->setData(static_cast<int>(eol));
        eolGroup_->addAction(act);
        connect(act, &QAction::triggered, this, [this, eol] {
            if (EditorTab* t = currentTab()) {
                t->setEol(eol);
            }
        });
    }
    edit->addSeparator();
    edit->addAction(a("copy-path"));
    edit->addAction(a("copy-name"));
    edit->addAction(a("read-only"));

    QMenu* search = bar->addMenu(tr("&Search"));
    search->addAction(a("find"));
    search->addAction(a("replace"));
    search->addAction(a("find-next"));
    search->addAction(a("find-previous"));
    search->addSeparator();
    search->addAction(a("go-to-line"));

    QMenu* view = bar->addMenu(tr("&View"));
    view->addAction(a("word-wrap"));
    view->addAction(a("show-whitespace"));
    view->addAction(a("show-eol"));
    view->addSeparator();
    view->addAction(a("zoom-in"));
    view->addAction(a("zoom-out"));
    view->addAction(a("zoom-reset"));
    view->addSeparator();
    view->addAction(a("next-tab"));
    view->addAction(a("previous-tab"));
    view->addAction(a("full-screen"));
    view->addSeparator();
    th::populateThemeMenu(view->addMenu(th::themedIcon(QStringLiteral("theme")), tr("&Theme")), this);
    th::populateStyleMenu(view->addMenu(th::themedIcon(QStringLiteral("widget-style")), tr("Widget &Style")), this);

    encodingMenu_ = bar->addMenu(tr("E&ncoding"));
    encodingGroup_ = new QActionGroup(this);
    for (const Encoding e : kAllEncodings) {
        QAction* act = encodingMenu_->addAction(encodingName(e));
        act->setCheckable(true);
        act->setData(static_cast<int>(e));
        act->setToolTip(tr("Save the document in %1 from now on").arg(encodingName(e)));
        encodingGroup_->addAction(act);
        connect(act, &QAction::triggered, this, [this, e] {
            EditorTab* t = currentTab();
            if (!t) {
                return;
            }
            if (!canEncode(t->editor()->toPlainText(), e)) {
                QMessageBox::warning(
                    this,
                    tr("Convert Encoding"),
                    tr("The document has characters that %1 cannot hold; they would be saved as "
                       "\"?\".")
                        .arg(encodingName(e)));
            }
            t->setEncoding(e);
        });
    }
    encodingMenu_->addSeparator();
    reloadEncodingMenu_ = encodingMenu_->addMenu(tr("&Reload with Encoding"));
    for (const Encoding e : kAllEncodings) {
        QAction* act = reloadEncodingMenu_->addAction(encodingName(e));
        connect(act, &QAction::triggered, this, [this, e] { reloadCurrent(e); });
    }

    languageMenu_ = bar->addMenu(tr("&Language"));
    rebuildLanguageMenu();

    QMenu* settingsMenu = bar->addMenu(tr("Se&ttings"));
    settingsMenu->addAction(a("font"));
    settingsMenu->addSeparator();
    settingsMenu->addAction(a("restore-session"));
    settingsMenu->addAction(a("backup-unsaved"));
    settingsMenu->addAction(a("single-instance"));

    QMenu* help = bar->addMenu(tr("&Help"));
    help->addAction(a("about"));
    help->addAction(a("about-qt"));
}

void MainWindow::buildToolBar() {
    toolBar_ = new th::ResponsiveToolBar(this);
    toolBar_->setObjectName(QStringLiteral("main-tool-bar"));
    toolBar_->setWindowTitle(tr("Tool Bar"));
    toolBar_->setMovable(false);
    auto a = [this](const char* name) {
        return action(QString::fromLatin1(name));
    };
    // The most used carry captions while there is room; the rest are icons.
    toolBar_->addLabeledAction(a("new"));
    toolBar_->addLabeledAction(a("open"));
    toolBar_->addLabeledAction(a("save"));
    toolBar_->addAction(a("save-all"));
    toolBar_->addAction(a("close"));
    toolBar_->addSeparator();
    toolBar_->addAction(a("undo"));
    toolBar_->addAction(a("redo"));
    toolBar_->addSeparator();
    toolBar_->addAction(a("cut"));
    toolBar_->addAction(a("copy"));
    toolBar_->addAction(a("paste"));
    toolBar_->addSeparator();
    toolBar_->addLabeledAction(a("find"));
    toolBar_->addAction(a("replace"));
    toolBar_->addSeparator();
    toolBar_->addAction(a("zoom-in"));
    toolBar_->addAction(a("zoom-out"));
    toolBar_->addSeparator();
    toolBar_->addAction(a("word-wrap"));
    toolBar_->addAction(a("show-whitespace"));
    // ResponsiveToolBar shows captions beside icons; the unregistered buttons are icons
    // only, as in Notepad++, so the captions of the registered ones have room.
    for (QAction* act : toolBar_->actions()) {
        if (auto* button = qobject_cast<QToolButton*>(toolBar_->widgetForAction(act));
            button && act != a("new") && act != a("open") && act != a("save") && act != a("find")) {
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }
    }
    addToolBar(Qt::TopToolBarArea, toolBar_);
}

void MainWindow::buildStatusBar() {
    QStatusBar* bar = statusBar();
    positionLabel_ = new QLabel(bar);
    lengthLabel_ = new QLabel(bar);
    auto menuButton = [bar](QMenu* menu, const QString& tip) {
        auto* button = new QToolButton(bar);
        button->setAutoRaise(true);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setMenu(menu);
        button->setToolTip(tip);
        return button;
    };
    eolButton_ = menuButton(eolMenu_, tr("Line endings"));
    encodingButton_ = menuButton(encodingMenu_, tr("Encoding"));
    languageButton_ = menuButton(languageMenu_, tr("Language"));
    modeLabel_ = new QLabel(bar);
    modeLabel_->setToolTip(tr("Insert or overwrite (Insert key)"));
    bar->addWidget(languageButton_);
    bar->addPermanentWidget(lengthLabel_);
    bar->addPermanentWidget(positionLabel_);
    bar->addPermanentWidget(eolButton_);
    bar->addPermanentWidget(encodingButton_);
    bar->addPermanentWidget(modeLabel_);
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    const QStringList files = settings::recentFiles();
    for (const QString& path : files) {
        QAction* act = recentMenu_->addAction(QDir::toNativeSeparators(path));
        act->setEnabled(QFileInfo::exists(path));
        connect(act, &QAction::triggered, this, [this, path] { openFiles({path}); });
    }
    if (files.isEmpty()) {
        recentMenu_->addAction(tr("(none)"))->setEnabled(false);
    } else {
        recentMenu_->addSeparator();
        connect(
            recentMenu_->addAction(tr("&Clear List")), &QAction::triggered, this, [] { settings::clearRecentFiles(); });
    }
}

void MainWindow::rebuildLanguageMenu() {
    languageMenu_->clear();
    delete languageGroup_;
    languageGroup_ = new QActionGroup(this);
    for (const Language& l : languages()) {
        QAction* act = languageMenu_->addAction(l.name);
        act->setCheckable(true);
        act->setData(l.id);
        languageGroup_->addAction(act);
        const QString id = l.id;
        connect(act, &QAction::triggered, this, [this, id] {
            EditorTab* t = currentTab();
            const Language* language = languageById(id);
            if (t && language) {
                t->setLanguage(*language);
            }
        });
        if (&l == &plainTextLanguage()) {
            languageMenu_->addSeparator();
        }
    }
}

void MainWindow::syncLanguageMenu() {
    const EditorTab* t = currentTab();
    for (QAction* act : languageGroup_->actions()) {
        act->setChecked(t && act->data().toString() == t->language().id);
    }
}

void MainWindow::syncEncodingMenus() {
    const EditorTab* t = currentTab();
    for (QAction* act : encodingGroup_->actions()) {
        act->setChecked(t && act->data().toInt() == static_cast<int>(t->encoding()));
    }
    for (QAction* act : eolGroup_->actions()) {
        act->setChecked(t && !t->hadMixedEols() && act->data().toInt() == static_cast<int>(t->eol()));
    }
    reloadEncodingMenu_->setEnabled(t && !t->isUntitled());
}

// ---- Tabs ---------------------------------------------------------------------------------

EditorTab* MainWindow::currentTab() const {
    return qobject_cast<EditorTab*>(tabs_->currentWidget());
}

EditorTab* MainWindow::tabAt(int index) const {
    return qobject_cast<EditorTab*>(tabs_->widget(index));
}

int MainWindow::tabCount() const {
    return tabs_->count();
}

EditorTab* MainWindow::tabForPath(const QString& path) const {
    const QString wanted = normalizedPath(path);
    for (int i = 0; i < tabCount(); ++i) {
        EditorTab* tab = tabAt(i);
        if (!tab->isUntitled() && samePath(tab->filePath(), wanted)) {
            return tab;
        }
    }
    return nullptr;
}

QString MainWindow::nextUntitledName() {
    QSet<QString> used;
    for (int i = 0; i < tabCount(); ++i) {
        used.insert(tabAt(i)->untitledName());
    }
    for (int n = 1;; ++n) {
        const QString name = tr("new %1").arg(n);
        if (!used.contains(name)) {
            return name;
        }
    }
}

EditorTab* MainWindow::createTab() {
    auto* tab = new EditorTab(tabs_);
    tab->setZoom(zoom_);
    tab->setDark(th::ThemeManager::instance().isDark());
    tab->setShowWhitespace(settings::showWhitespace(), settings::showLineEndings());
    connectTab(tab);
    return tab;
}

void MainWindow::addTab(EditorTab* tab, bool makeCurrent) {
    const int index = tabs_->addTab(tab, tab->displayName());
    updateTabTitle(tab);
    if (makeCurrent) {
        tabs_->setCurrentIndex(index);
    }
}

EditorTab* MainWindow::newDocument() {
    EditorTab* tab = createTab();
    tab->setUntitledName(nextUntitledName());
    addTab(tab, true);
    tab->editor()->setFocus();
    return tab;
}

void MainWindow::connectTab(EditorTab* tab) {
    connect(tab, &EditorTab::infoChanged, this, [this, tab] {
        updateTabTitle(tab);
        if (tab == currentTab()) {
            updateWindowTitle();
            updateStatus();
            updateActions();
        }
    });
    connect(tab, &EditorTab::loaded, this, [this, tab] { tabLoaded(tab); });
    connect(tab, &EditorTab::filesDropped, this, [this](const QList<QUrl>& urls) {
        QStringList paths;
        for (const QUrl& url : urls) {
            paths << url.toLocalFile();
        }
        openFiles(paths);
    });
    ed::CodeEditor* editor = tab->editor();
    auto ifCurrent = [this, tab](auto f) {
        return [this, tab, f] {
            if (tab == currentTab()) {
                (this->*f)();
            }
        };
    };
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, ifCurrent(&MainWindow::updatePosition));
    connect(editor, &QPlainTextEdit::selectionChanged, this, ifCurrent(&MainWindow::updatePosition));
    connect(editor, &QPlainTextEdit::textChanged, this, ifCurrent(&MainWindow::updatePosition));
    connect(editor, &ed::CodeEditor::overwriteModeChanged, this, ifCurrent(&MainWindow::updatePosition));
    connect(editor, &QPlainTextEdit::undoAvailable, this, ifCurrent(&MainWindow::updateActions));
    connect(editor, &QPlainTextEdit::redoAvailable, this, ifCurrent(&MainWindow::updateActions));
    connect(editor, &QPlainTextEdit::copyAvailable, this, ifCurrent(&MainWindow::updateActions));
}

void MainWindow::tabLoaded(EditorTab* tab) {
    if (tab->loadState() == EditorTab::LoadState::Failed) {
        const QString path = tab->filePath();
        const QString error = tab->error();
        closeTab(tab, true);
        QMessageBox::warning(
            this, tr("Open File"), tr("\"%1\" could not be opened:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    watch(tab);
    if (!tab->isUntitled() && tab->knownModified().isValid()) {
        // Changes restored from a backup were made to the file as it was then.
        const QFileInfo info(tab->filePath());
        if (!info.exists() || info.lastModified() != tab->knownModified()) {
            changedOnDisk_.insert(tab->filePath());
            changeTimer_->start();
        }
    }
    if (tab == currentTab()) {
        if (tab->looksBinary()) {
            statusBar()->showMessage(tr("%1 looks like a binary file.").arg(tab->displayName()), 5000);
        } else if (tab->highlightingSuppressed()) {
            statusBar()->showMessage(
                tr("%1 is large (%2): shown without syntax highlighting.")
                    .arg(tab->displayName(), formatByteSize(tab->fileSize())),
                8000);
        }
        updateStatus();
        updateActions();
    }
}

void MainWindow::currentTabChanged() {
    EditorTab* tab = currentTab();
    if (tab) {
        tab->ensureLoaded();
        tab->editor()->setFocus();
    }
    updateWindowTitle();
    updateStatus();
    updateActions();
}

void MainWindow::updateTabTitle(EditorTab* tab) {
    const int index = tabs_->indexOf(tab);
    if (index < 0) {
        return;
    }
    tabs_->setTabText(index, tab->displayName());
    tabs_->setTabToolTip(index, tab->isUntitled() ? tab->displayName() : QDir::toNativeSeparators(tab->filePath()));
    QString icon = QStringLiteral("file");
    if (tab->isModified()) {
        icon = QStringLiteral("file-modified");
    } else if (tab->isReadOnly()) {
        icon = QStringLiteral("read-only");
    }
    // Only when it changes: setTabIcon re-lays out the tab bar. The tab's data holds the
    // name of the icon it shows.
    if (tabs_->tabBar()->tabData(index).toString() != icon) {
        tabs_->tabBar()->setTabData(index, icon);
        tabs_->setTabIcon(index, th::themedIcon(icon));
    }
}

void MainWindow::updateWindowTitle() {
    const EditorTab* tab = currentTab();
    if (!tab) {
        setWindowTitle(QStringLiteral("Neko Pad"));
        setWindowModified(false);
        return;
    }
    const QString name = tab->isUntitled() ? tab->displayName() : QDir::toNativeSeparators(tab->filePath());
    setWindowTitle(tr("%1[*] - Neko Pad").arg(name));
    setWindowModified(tab->isModified());
    setWindowFilePath(tab->isUntitled() ? QString() : tab->filePath());
}

void MainWindow::updateStatus() {
    const EditorTab* tab = currentTab();
    if (!tab) {
        eolButton_->setText({});
        encodingButton_->setText({});
        languageButton_->setText({});
        positionLabel_->clear();
        lengthLabel_->clear();
        modeLabel_->clear();
        return;
    }
    eolButton_->setText(tab->hadMixedEols() ? tr("Mixed (%1)").arg(eolShortName(tab->eol())) : eolName(tab->eol()));
    encodingButton_->setText(encodingName(tab->encoding()));
    QString language = tab->language().name;
    if (tab->highlightingSuppressed()) {
        language = tr("%1 (no highlighting: large file)").arg(language);
    }
    if (tab->loadState() == EditorTab::LoadState::Loading) {
        language = tr("Loading...");
    }
    languageButton_->setText(language);
    syncLanguageMenu();
    syncEncodingMenus();
    updatePosition();
}

void MainWindow::updatePosition() {
    const EditorTab* tab = currentTab();
    if (!tab) {
        return;
    }
    const ed::CodeEditor* editor = tab->editor();
    const QTextCursor c = editor->textCursor();
    const QTextBlock block = c.block();
    const int column = lines::displayColumn(block.text(), c.positionInBlock(), ed::CodeEditor::kTabWidth) + 1;
    QString selection;
    if (editor->hasBlockSelection()) {
        const auto& box = *editor->blockSelection();
        selection = tr("Sel %1 x %2").arg(box.bottom() - box.top() + 1).arg(box.right() - box.left());
    } else {
        const int chars = c.selectionEnd() - c.selectionStart();
        int selectedLines = 0;
        if (chars > 0) {
            const QTextDocument* doc = editor->document();
            selectedLines =
                doc->findBlock(c.selectionEnd()).blockNumber() - doc->findBlock(c.selectionStart()).blockNumber() + 1;
        }
        selection = tr("Sel %L1 | %L2").arg(chars).arg(selectedLines);
    }
    positionLabel_->setText(tr("Ln %L1, Col %L2    %3").arg(block.blockNumber() + 1).arg(column).arg(selection));
    const QTextDocument* doc = editor->document();
    lengthLabel_->setText(tr("length: %L1    lines: %L2").arg(doc->characterCount() - 1).arg(doc->blockCount()));
    modeLabel_->setText(editor->isOverwriteMode() ? tr("OVR") : tr("INS"));
}

void MainWindow::updateActions() {
    const EditorTab* tab = currentTab();
    const bool has = tab != nullptr;
    const ed::CodeEditor* editor = has ? tab->editor() : nullptr;
    action(QStringLiteral("undo"))->setEnabled(has && editor->document()->isUndoAvailable());
    action(QStringLiteral("redo"))->setEnabled(has && editor->document()->isRedoAvailable());
    const bool selection = has && (editor->textCursor().hasSelection() || editor->hasBlockSelection());
    action(QStringLiteral("cut"))->setEnabled(selection && !editor->isReadOnly());
    action(QStringLiteral("copy"))->setEnabled(selection);
    action(QStringLiteral("save"))->setEnabled(has && (tab->isModified() || tab->isUntitled()));
    action(QStringLiteral("reload"))->setEnabled(has && !tab->isUntitled());
    action(QStringLiteral("open-folder"))->setEnabled(has && !tab->isUntitled());
    action(QStringLiteral("copy-path"))->setEnabled(has && !tab->isUntitled());
    action(QStringLiteral("reopen-closed"))->setEnabled(!closedPaths_.isEmpty());
    QAction* readOnly = action(QStringLiteral("read-only"));
    const QSignalBlocker blocker(readOnly);
    readOnly->setChecked(has && tab->isReadOnly());
}

// ---- Opening, saving, closing ---------------------------------------------------------------

void MainWindow::openFiles(const QStringList& paths) {
    EditorTab* last = nullptr;
    for (const QString& arg : paths) {
        if (arg.isEmpty()) {
            continue;
        }
        const auto [rawPath, line] = splitLineSuffix(arg);
        const QString path = normalizedPath(rawPath);
        if (EditorTab* open = tabForPath(path)) {
            if (line > 0) {
                open->goToLine(line - 1);
            }
            last = open;
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir()) {
            continue;
        }
        if (!info.exists()) {
            const auto answer = QMessageBox::question(
                this, tr("Open File"), tr("\"%1\" does not exist. Create it?").arg(QDir::toNativeSeparators(path)));
            if (answer != QMessageBox::Yes) {
                continue;
            }
            QFile create(path);
            if (!create.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(
                    this,
                    tr("Open File"),
                    tr("\"%1\" could not be created:\n%2").arg(QDir::toNativeSeparators(path), create.errorString()));
                continue;
            }
        } else if (info.size() > kAskBeforeOpening) {
            const auto answer = QMessageBox::question(
                this,
                tr("Open File"),
                tr("\"%1\" is %2. Open it anyway?").arg(info.fileName(), formatByteSize(info.size())));
            if (answer != QMessageBox::Yes) {
                continue;
            }
        }

        // An untouched "new 1" makes way for the first file opened, as in Notepad++.
        EditorTab* pristine = nullptr;
        if (tabCount() == 1) {
            EditorTab* only = tabAt(0);
            if (only->isUntitled() && !only->isModified() && only->editor()->document()->isEmpty()) {
                pristine = only;
            }
        }

        EditorTab* tab = createTab();
        tab->load(path);
        if (line > 0) {
            tab->goToLine(line - 1); // after load(): it waits for the text
        }
        addTab(tab, false);
        if (pristine) {
            closeTab(pristine, true);
        }
        settings::addRecentFile(path);
        settings::setLastDirectory(info.absolutePath());
        last = tab;
    }
    if (last) {
        tabs_->setCurrentWidget(last);
    }
}

void MainWindow::openFileDialog() {
    QString directory = settings::lastDirectory();
    if (const EditorTab* t = currentTab(); t && !t->isUntitled()) {
        directory = QFileInfo(t->filePath()).absolutePath();
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open"), directory, fileDialogFilter());
    openFiles(files);
}

bool MainWindow::saveTab(EditorTab* tab) {
    if (!ensureReady(tab)) {
        return false;
    }
    if (tab->isUntitled()) {
        return saveTabAs(tab);
    }
    if (!canEncode(tab->editor()->toPlainText(), tab->encoding())) {
        const auto answer = QMessageBox::question(
            this,
            tr("Save"),
            tr("\"%1\" has characters that %2 cannot hold. Save it as UTF-8 instead?")
                .arg(tab->displayName(), encodingName(tab->encoding())),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (answer == QMessageBox::Cancel) {
            return false;
        }
        if (answer == QMessageBox::Yes) {
            tab->setEncoding(Encoding::Utf8);
        }
    }
    const SaveResult result = tab->save();
    if (!result.ok) {
        QMessageBox::warning(
            this,
            tr("Save"),
            tr("\"%1\" could not be saved:\n%2").arg(QDir::toNativeSeparators(tab->filePath()), result.error));
        return false;
    }
    watch(tab); // an atomic save replaces the file: watch the new one
    discardBackup(tab);
    statusBar()->showMessage(tr("Saved %1").arg(tab->displayName()), 3000);
    return true;
}

bool MainWindow::saveTabAs(EditorTab* tab) {
    if (!ensureReady(tab)) {
        return false;
    }
    QString suggestion;
    if (tab->isUntitled()) {
        suggestion = QDir(settings::lastDirectory()).filePath(tab->displayName());
    } else {
        suggestion = tab->filePath();
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggestion, fileDialogFilter());
    if (path.isEmpty()) {
        return false;
    }
    const QString oldPath = tab->filePath();
    const SaveResult result = tab->saveAs(path);
    if (!result.ok) {
        QMessageBox::warning(
            this,
            tr("Save As"),
            tr("\"%1\" could not be saved:\n%2").arg(QDir::toNativeSeparators(path), result.error));
        return false;
    }
    if (!oldPath.isEmpty() && !samePath(oldPath, path)) {
        unwatch(oldPath);
    }
    watch(tab);
    discardBackup(tab);
    settings::addRecentFile(normalizedPath(path));
    settings::setLastDirectory(QFileInfo(path).absolutePath());
    return true;
}

bool MainWindow::saveAll() {
    bool all = true;
    for (int i = 0; i < tabCount(); ++i) {
        EditorTab* tab = tabAt(i);
        if (tab->isModified()) {
            all = saveTab(tab) && all;
        }
    }
    return all;
}

bool MainWindow::closeTab(EditorTab* tab, bool force) {
    const int index = tabs_->indexOf(tab);
    if (index < 0) {
        return true;
    }
    if (!force && tab->isModified()) {
        tabs_->setCurrentIndex(index);
        const QPointer<EditorTab> guard(tab);
        if (!ensureReady(tab)) {
            return !guard || tabs_->indexOf(tab) < 0; // closed as it failed to load
        }
        const auto answer = QMessageBox::question(
            this,
            tr("Close"),
            tr("Save changes to \"%1\"?").arg(tab->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveTab(tab))) {
            return false;
        }
    }
    const QString path = tab->filePath();
    if (tab->loadState() != EditorTab::LoadState::Failed) {
        discardBackup(tab); // a backup that could not be read stays for the next start
    }
    tabs_->removeTab(tabs_->indexOf(tab));
    if (!path.isEmpty()) {
        unwatch(path);
        closedPaths_.removeAll(path);
        closedPaths_ << path;
    }
    tab->deleteLater();
    if (tabCount() == 0 && !closing_) {
        newDocument();
    }
    updateActions();
    return true;
}

void MainWindow::closeOtherTabs(EditorTab* keep) {
    for (int i = tabCount() - 1; i >= 0; --i) {
        if (EditorTab* tab = tabAt(i); tab != keep && !closeTab(tab)) {
            return;
        }
    }
}

void MainWindow::closeTabsToTheRight(EditorTab* tab) {
    const int from = tabs_->indexOf(tab);
    for (int i = tabCount() - 1; i > from; --i) {
        if (!closeTab(tabAt(i))) {
            return;
        }
    }
}

void MainWindow::closeAllTabs() {
    for (int i = tabCount() - 1; i >= 0; --i) {
        if (!closeTab(tabAt(i))) {
            return;
        }
    }
}

void MainWindow::reopenClosedTab() {
    while (!closedPaths_.isEmpty()) {
        const QString path = closedPaths_.takeLast();
        if (QFileInfo::exists(path)) {
            openFiles({path});
            return;
        }
    }
}

void MainWindow::reloadCurrent(std::optional<Encoding> forced) {
    EditorTab* tab = currentTab();
    if (!tab || tab->isUntitled()) {
        return;
    }
    if (tab->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("Reload"), tr("Reload \"%1\" and lose the changes made here?").arg(tab->displayName()));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    tab->reload(forced);
}

void MainWindow::showTabContextMenu(const QPoint& pos) {
    const int index = tabs_->tabBar()->tabAt(pos);
    EditorTab* tab = tabAt(index);
    if (!tab) {
        return;
    }
    tabs_->setCurrentIndex(index);
    QMenu menu(this);
    menu.addAction(action(QStringLiteral("close")));
    menu.addAction(action(QStringLiteral("close-others")));
    connect(menu.addAction(tr("Close Tabs to the &Right")), &QAction::triggered, this, [this, tab] {
        closeTabsToTheRight(tab);
    });
    menu.addSeparator();
    menu.addAction(action(QStringLiteral("save")));
    menu.addAction(action(QStringLiteral("save-as")));
    menu.addAction(action(QStringLiteral("reload")));
    menu.addSeparator();
    menu.addAction(action(QStringLiteral("copy-path")));
    menu.addAction(action(QStringLiteral("copy-name")));
    menu.addAction(action(QStringLiteral("open-folder")));
    menu.addSeparator();
    menu.addAction(action(QStringLiteral("read-only")));
    menu.exec(tabs_->tabBar()->mapToGlobal(pos));
}

void MainWindow::goToLineDialog() {
    EditorTab* tab = currentTab();
    if (!tab) {
        return;
    }
    const int lines = tab->editor()->document()->blockCount();
    bool ok = false;
    const int line = QInputDialog::getInt(
        this, tr("Go to Line"), tr("Line (1 - %L1):").arg(lines), tab->cursorLineColumn().first + 1, 1, lines, 1, &ok);
    if (ok) {
        tab->goToLine(line - 1);
        tab->editor()->setFocus();
    }
}

void MainWindow::chooseFont() {
    bool ok = false;
    const QFont font = QFontDialog::getFont(
        &ok, ed::EditorDefaults::instance().font(), this, tr("Editor Font"), QFontDialog::MonospacedFonts);
    if (ok) {
        settings::setEditorFont(font);
        ed::EditorDefaults::instance().setFont(font);
    }
}

void MainWindow::setZoom(int zoom) {
    zoom_ = std::clamp(zoom, -8, 40);
    settings::setZoom(zoom_);
    for (int i = 0; i < tabCount(); ++i) {
        tabAt(i)->setZoom(zoom_);
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this,
        tr("About Neko Pad"),
        tr("<h3>Neko Pad %1</h3>"
           "<p>A fast, cross-platform text and source code editor in C++23 and Qt %2.</p>"
           "<p>The editor and the look come from "
           "<a href=\"https://github.com/fhuber83/NekoControls\">NekoControls</a>; the icons are "
           "<a href=\"https://lucide.dev\">Lucide</a>.</p>"
           "<p>MIT License.</p>")
            .arg(QStringLiteral(NEKOPAD_VERSION_STRING), QString::fromLatin1(qVersion())));
}

// ---- Files changed elsewhere ---------------------------------------------------------------

void MainWindow::watch(EditorTab* tab) {
    if (tab->isUntitled() || !QFileInfo::exists(tab->filePath())) {
        return;
    }
    if (!watcher_->files().contains(tab->filePath())) {
        watcher_->addPath(tab->filePath());
    }
}

void MainWindow::unwatch(const QString& path) {
    for (int i = 0; i < tabCount(); ++i) {
        if (samePath(tabAt(i)->filePath(), path)) {
            return; // still open in another tab
        }
    }
    watcher_->removePath(path);
}

void MainWindow::fileChangedOnDisk(const QString& path) {
    changedOnDisk_.insert(path);
    // An editor that saves by renaming a new file over the old one ends the watch.
    if (QFileInfo::exists(path) && !watcher_->files().contains(path)) {
        watcher_->addPath(path);
    }
    changeTimer_->start();
}

void MainWindow::checkChangedFiles() {
    if (checkingChanges_ || changedOnDisk_.isEmpty()) {
        return;
    }
    if (!isActiveWindow()) {
        return; // asked when the window is active again (changeEvent)
    }
    checkingChanges_ = true;
    const QSet<QString> paths = std::exchange(changedOnDisk_, {});
    for (const QString& path : paths) {
        EditorTab* tab = tabForPath(path);
        if (!tab || tab->loadState() != EditorTab::LoadState::Ready) {
            continue; // a tab not read yet reads the new file anyway
        }
        const QFileInfo info(path);
        if (!info.exists()) {
            tab->editor()->document()->setModified(true);
            statusBar()->showMessage(
                tr("%1 was deleted or moved by another program.").arg(QDir::toNativeSeparators(path)), 8000);
            continue;
        }
        if (info.lastModified() == tab->knownModified()) {
            continue; // our own save
        }
        const QString question =
            tab->isModified()
                ? tr("\"%1\" was changed by another program.\n\nReload it and lose the changes made here?")
                : tr("\"%1\" was changed by another program.\n\nReload it?");
        tabs_->setCurrentWidget(tab);
        const auto answer =
            QMessageBox::question(this, tr("File Changed"), question.arg(QDir::toNativeSeparators(path)));
        if (answer == QMessageBox::Yes) {
            tab->reload();
        } else {
            tab->setKnownModified(info.lastModified());
        }
    }
    checkingChanges_ = false;
    if (!changedOnDisk_.isEmpty()) {
        changeTimer_->start();
    }
}

// ---- Session ---------------------------------------------------------------------------------

void MainWindow::restoreSession() {
    const QList<settings::SessionFile> files = settings::sessionFiles();
    const int current = settings::sessionCurrentIndex();
    EditorTab* currentTabToBe = nullptr;
    int index = 0;
    for (const settings::SessionFile& f : files) {
        const int i = index++;
        const bool fromBackup = !f.backup.isEmpty() && QFileInfo::exists(f.backup);
        if (!f.path.isEmpty() && tabForPath(f.path)) {
            continue;
        }
        if (!fromBackup && (f.path.isEmpty() || !QFileInfo::exists(f.path))) {
            continue; // a new document whose backup is gone, or a file that is gone
        }
        EditorTab* tab = createTab();
        if (!f.languageId.isEmpty()) {
            if (const Language* l = languageById(f.languageId)) {
                tab->setLanguage(*l);
            }
        }
        if (f.path.isEmpty()) {
            tab->setUntitledName(f.untitledName.isEmpty() ? nextUntitledName() : f.untitledName);
        }
        if (fromBackup) {
            // Even if the file is gone: the unsaved text is not.
            tab->setPendingBackup({f.backup, f.path, f.encoding, f.eol, f.fileModified});
        } else {
            tab->setPendingFile(f.path);
        }
        tab->goToLine(f.line, f.column);
        // Adding a tab makes the first one current, which reads it: hold that until the
        // session's current tab is known.
        const QSignalBlocker blocker(tabs_);
        addTab(tab, false);
        if (i == current || !currentTabToBe) {
            currentTabToBe = tab;
        }
    }
    if (currentTabToBe) {
        tabs_->setCurrentWidget(currentTabToBe);
        currentTabChanged(); // in case it was current already, with the signal blocked
    }
}

QList<settings::SessionFile> MainWindow::sessionEntries(int* current) const {
    QList<settings::SessionFile> files;
    *current = 0;
    for (int i = 0; i < tabCount(); ++i) {
        const EditorTab* tab = tabAt(i);
        const bool backedUp = !tab->backupPath().isEmpty();
        if (tab->isUntitled() && !backedUp) {
            continue;
        }
        if (tab == currentTab()) {
            *current = static_cast<int>(files.size());
        }
        const auto [line, column] = tab->sessionCursor();
        settings::SessionFile f;
        f.path = tab->filePath();
        f.line = line;
        f.column = column;
        f.languageId = tab->languageChosen() ? tab->language().id : QString();
        if (tab->isUntitled()) {
            f.untitledName = tab->untitledName();
        }
        if (backedUp) {
            f.backup = tab->backupPath();
            f.encoding = tab->encoding();
            f.eol = tab->eol();
            f.fileModified = tab->isUntitled() ? QDateTime() : tab->knownModified();
        }
        files << f;
    }
    return files;
}

void MainWindow::saveSession() {
    int current = 0;
    QList<settings::SessionFile> files = sessionEntries(&current);
    settings::setSession(files, current);
    lastSession_ = std::move(files);
    lastSessionCurrent_ = current;
}

// ---- Unsaved changes across restarts ---------------------------------------------------------

bool MainWindow::keepsUnsavedChanges() const {
    return useSession_ && settings::restoreSession() && settings::backupUnsaved();
}

bool MainWindow::ensureReady(EditorTab* tab) {
    const QPointer<EditorTab> guard(tab);
    tab->ensureLoaded();
    if (tab->loadState() == EditorTab::LoadState::Loading) {
        QEventLoop loop;
        connect(tab, &EditorTab::loaded, &loop, &QEventLoop::quit);
        connect(tab, &QObject::destroyed, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return guard && tabs_->indexOf(tab) >= 0 && tab->loadState() == EditorTab::LoadState::Ready;
}

void MainWindow::backupTab(EditorTab* tab, bool now) {
    if (tab->loadState() != EditorTab::LoadState::Ready) {
        return; // its text is not here yet: whatever backup it has is what it is
    }
    // An empty new document is nothing to keep, modified or not.
    const bool unsaved = tab->isModified() && !(tab->isUntitled() && tab->editor()->document()->isEmpty());
    if (!unsaved) {
        discardBackup(tab);
        return;
    }
    if (tab->backupPath().isEmpty()) {
        tab->setBackupPath(backups_->newPath(tab->displayName()));
    } else if (backups_->failed(tab->backupPath())) {
        // Try again.
    } else if (!tab->backupDirty()) {
        return;
    } else if (
        !now && tab->editor()->document()->characterCount() > kLargeBackupChars && tab->msSinceBackup() >= 0 &&
        tab->msSinceBackup() < kLargeBackupIntervalMs) {
        return;
    }
    backups_->write(tab->backupPath(), tab->text());
    tab->setBackupDirty(false);
}

void MainWindow::discardBackup(EditorTab* tab) {
    if (const QString path = tab->backupPath(); !path.isEmpty()) {
        backups_->remove(path);
        tab->setBackupPath({});
    }
}

void MainWindow::backupNow() {
    if (!useSession_ || !settings::restoreSession()) {
        return;
    }
    if (keepsUnsavedChanges()) {
        for (int i = 0; i < tabCount(); ++i) {
            backupTab(tabAt(i), false);
        }
    }
    int current = 0;
    QList<settings::SessionFile> files = sessionEntries(&current);
    if (files != lastSession_ || current != lastSessionCurrent_) {
        settings::setSession(files, current);
        lastSession_ = std::move(files);
        lastSessionCurrent_ = current;
    }
}

// ---- Events ----------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event) {
    const bool keep = keepsUnsavedChanges();
    if (keep) {
        for (int i = 0; i < tabCount(); ++i) {
            backupTab(tabAt(i), true);
        }
        backups_->waitForDone();
    }
    for (int i = 0; i < tabCount(); ++i) {
        EditorTab* tab = tabAt(i);
        if (!tab->isModified()) {
            continue;
        }
        if (keep) {
            const bool empty = tab->loadState() == EditorTab::LoadState::Ready && tab->isUntitled() &&
                               tab->editor()->document()->isEmpty();
            const bool safe = !tab->backupPath().isEmpty() && !backups_->failed(tab->backupPath());
            if (empty || safe) {
                continue; // nothing to lose, or it is kept for the next start
            }
        }
        tabs_->setCurrentIndex(i);
        const QPointer<EditorTab> guard(tab);
        if (!ensureReady(tab)) {
            if (guard && tabs_->indexOf(tab) >= 0) {
                event->ignore();
                return;
            }
            --i; // it could not be read and was closed (its backup stays)
            continue;
        }
        const auto answer = QMessageBox::question(
            this,
            tr("Exit"),
            tr("Save changes to \"%1\"?").arg(tab->displayName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveTab(tab))) {
            event->ignore();
            return;
        }
    }
    closing_ = true;
    backupTimer_->stop();
    if (useSession_) {
        if (!keep) {
            // Asked about and saved or discarded: no backup is wanted any more.
            for (int i = 0; i < tabCount(); ++i) {
                discardBackup(tabAt(i));
            }
        }
        if (settings::restoreSession()) {
            saveSession();
        } else {
            settings::setSession({}, 0);
        }
    }
    backups_->waitForDone();
    settings::setWindowGeometry(saveGeometry(), saveState());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        openFiles(paths);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tabs_->tabBar() && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (EditorTab* tab = tabAt(tabs_->tabBar()->tabAt(mouse->position().toPoint()))) {
                closeTab(tab);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && !changedOnDisk_.isEmpty()) {
        changeTimer_->start();
    }
    QMainWindow::changeEvent(event);
}

QString MainWindow::statusPositionText() const {
    return positionLabel_->text();
}
QString MainWindow::statusEncodingText() const {
    return encodingButton_->text();
}
QString MainWindow::statusEolText() const {
    return eolButton_->text();
}
QString MainWindow::statusLanguageText() const {
    return languageButton_->text();
}

} // namespace neko::pad::ui
