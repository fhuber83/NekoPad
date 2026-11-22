// Tests of nekopad_ui: the window, tabs, loading and saving, line operations, the
// highlighter, the session and the single-instance link. Runs offscreen, with settings
// of its own (QStandardPaths test mode, an application name of its own).
//
// NEKOPAD_TEST_SCREENSHOTS=<dir> also saves the window in a light and a dark theme there.

#include "core/language.hpp"
#include "ui/editor_tab.hpp"
#include "ui/main_window.hpp"
#include "ui/settings.hpp"
#include "ui/single_instance.hpp"
#include "ui/syntax_highlighter.hpp"

#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_find_bar.hpp"
#include "neko/editor/editor_icons.hpp"
#include "neko/editor/markdown_highlighter.hpp"
#include "neko/theme/theme_manager.hpp"
#include "neko/theme/themed_icon.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

namespace th = neko::theme;
using neko::pad::ui::EditorTab;
using neko::pad::ui::MainWindow;

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

[[nodiscard]] QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

/// The format the highlighter gave the character at `column` of `line`.
[[nodiscard]] QTextCharFormat formatAt(QTextDocument* doc, int line, int column) {
    const QTextBlock block = doc->findBlockByNumber(line);
    for (const QTextLayout::FormatRange& range : block.layout()->formats()) {
        if (column >= range.start && column < range.start + range.length) {
            return range.format;
        }
    }
    return {};
}

} // namespace

class TestUi final : public QObject {
    Q_OBJECT

private slots:
    /// An icon name the map does not have is a bug, wherever a test meets it.
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("^themedIcon: "))); }

    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("Neko"));
        QCoreApplication::setApplicationName(QStringLiteral("Neko Pad Test"));
        QSettings().clear();
        QVERIFY(dir_.isValid());
        th::ThemeManager::setRestartNoticeAction(th::ThemeRestartAction::RestartLater);
        th::ThemeManager::instance().setThemeMode(th::ThemeMode::Light);
        neko::editor::setEditorIconProvider(&th::themedIcon);
        neko::pad::ui::applyStoredEditorSettings();
    }

    void testEveryIconTheControlsAskForIsMapped() {
        for (const QString& name : neko::editor::editorIconNames()) {
            QVERIFY2(th::hasThemedIcon(name), qPrintable(name));
        }
    }

    void testWindowStartsUsable() {
        MainWindow window;
        window.newDocument();
        QCOMPARE(window.tabCount(), 1);
        QCOMPARE(window.currentTab()->displayName(), QStringLiteral("new 1"));
        QVERIFY(window.windowTitle().contains(QStringLiteral("new 1")));
        QVERIFY(window.statusPositionText().startsWith(QStringLiteral("Ln 1, Col 1")));
        QCOMPARE(window.statusEncodingText(), QStringLiteral("UTF-8"));
        // Every menu has entries, and every action with an icon has a mapped one (init()).
        for (QAction* menu : window.menuBar()->actions()) {
            QVERIFY(menu->menu() && !menu->menu()->actions().isEmpty());
        }
        window.newDocument();
        QCOMPARE(window.currentTab()->displayName(), QStringLiteral("new 2"));
    }

    void testOpenDetectsEncodingEolAndLanguage() {
        const QString path = dir_.filePath(QStringLiteral("main.cpp"));
        writeFile(path, "\xEF\xBB\xBF#include <cstdio>\r\nint main() { return 0; } // done\r\n");
        MainWindow window;
        window.newDocument();
        window.openFiles({path});
        // The untouched "new 1" made way for the file.
        QCOMPARE(window.tabCount(), 1);
        EditorTab* tab = window.currentTab();
        QTRY_COMPARE(tab->loadState(), EditorTab::LoadState::Ready);
        QCOMPARE(tab->encoding(), neko::pad::Encoding::Utf8Bom);
        QCOMPARE(tab->eol(), neko::pad::Eol::CrLf);
        QCOMPARE(tab->language().id, QStringLiteral("cpp"));
        QCOMPARE(tab->editor()->toPlainText(), QStringLiteral("#include <cstdio>\nint main() { return 0; } // done\n"));
        QVERIFY(!tab->isModified());
        QCOMPARE(window.statusEncodingText(), QStringLiteral("UTF-8 BOM"));
        QCOMPARE(window.statusEolText(), QStringLiteral("Windows (CR LF)"));
        QCOMPARE(window.statusLanguageText(), QStringLiteral("C++"));
        QVERIFY(qobject_cast<QSyntaxHighlighter*>(tab->highlighter()));

        // Opening it again switches to it.
        window.newDocument();
        window.openFiles({path});
        QCOMPARE(window.tabCount(), 2);
        QCOMPARE(window.currentTab(), tab);
    }

    void testOpenWithLineSuffix() {
        const QString path = dir_.filePath(QStringLiteral("lines.txt"));
        writeFile(path, "one\ntwo\nthree\nfour\n");
        MainWindow window;
        window.openFiles({path + QStringLiteral(":3")});
        EditorTab* tab = window.currentTab();
        QVERIFY(tab);
        QTRY_COMPARE(tab->loadState(), EditorTab::LoadState::Ready);
        QCOMPARE(tab->cursorLineColumn().first, 2);
    }

    void testSaveKeepsEncodingEolAndNonBreakingSpaces() {
        const QString path = dir_.filePath(QStringLiteral("keep.txt"));
        writeFile(
            path,
            "\xEF\xBB\xBF"
            "a\xC2\xA0"
            "b\r\nc\r\n"); // "a<NBSP>b"
        MainWindow window;
        window.openFiles({path});
        EditorTab* tab = window.currentTab();
        QTRY_COMPARE(tab->loadState(), EditorTab::LoadState::Ready);
        QTextCursor c(tab->editor()->document());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("d"));
        QVERIFY(tab->isModified());
        QVERIFY(window.saveTab(tab));
        QVERIFY(!tab->isModified());
        QCOMPARE(
            readFile(path),
            QByteArray(
                "\xEF\xBB\xBF"
                "a\xC2\xA0"
                "b\r\nc\r\nd"));

        // Converting: line endings and encoding are what is written next.
        tab->setEol(neko::pad::Eol::Lf);
        tab->setEncoding(neko::pad::Encoding::Utf8);
        QVERIFY(tab->isModified());
        QVERIFY(window.saveTab(tab));
        QCOMPARE(
            readFile(path),
            QByteArray(
                "a\xC2\xA0"
                "b\nc\nd"));
    }

    void testSaveAsChangesLanguage() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        tab->editor()->setPlainText(QStringLiteral("def f():\n    return 1\n"));
        QCOMPARE(tab->language().id, QStringLiteral("text"));
        const QString path = dir_.filePath(QStringLiteral("script.py"));
        QVERIFY(tab->saveAs(path).ok);
        QCOMPARE(tab->filePath(), path);
        QCOMPARE(tab->language().id, QStringLiteral("python"));
        QVERIFY(tab->highlighter());
        QCOMPARE(window.currentTab()->displayName(), QStringLiteral("script.py"));
    }

    void testLineOperations() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        neko::editor::CodeEditor* editor = tab->editor();
        editor->setPlainText(QStringLiteral("one\ntwo\nthree"));
        tab->goToLine(1);

        window.action(QStringLiteral("duplicate"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("one\ntwo\ntwo\nthree"));

        window.action(QStringLiteral("move-up"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("two\none\ntwo\nthree"));
        QCOMPARE(tab->cursorLineColumn().first, 0);

        window.action(QStringLiteral("move-down"))->trigger();
        window.action(QStringLiteral("move-down"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("one\ntwo\ntwo\nthree"));

        window.action(QStringLiteral("delete-line"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("one\ntwo\nthree"));

        editor->undo();
        QCOMPARE(editor->toPlainText(), QStringLiteral("one\ntwo\ntwo\nthree"));

        // Bulk operations work on the whole document without a selection.
        window.action(QStringLiteral("remove-duplicates"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("one\ntwo\nthree"));
        editor->moveCursor(QTextCursor::Start);
        window.action(QStringLiteral("sort-desc"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("two\nthree\none"));

        // Case of the word under the cursor.
        editor->moveCursor(QTextCursor::Start);
        window.action(QStringLiteral("upper-case"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("TWO\nthree\none"));

        // Deleting the last line takes the line break before it.
        tab->goToLine(2);
        window.action(QStringLiteral("delete-line"))->trigger();
        QCOMPARE(editor->toPlainText(), QStringLiteral("TWO\nthree"));
    }

    void testToggleCommentFollowsTheLanguage() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        tab->editor()->setPlainText(QStringLiteral("x = 1\ny = 2"));
        tab->setLanguage(*neko::pad::languageById(u"python"));
        tab->editor()->selectAll();
        window.action(QStringLiteral("toggle-comment"))->trigger();
        QCOMPARE(tab->editor()->toPlainText(), QStringLiteral("# x = 1\n# y = 2"));
        window.action(QStringLiteral("toggle-comment"))->trigger();
        QCOMPARE(tab->editor()->toPlainText(), QStringLiteral("x = 1\ny = 2"));

        tab->setLanguage(*neko::pad::languageById(u"html"));
        tab->editor()->setPlainText(QStringLiteral("<p>hi</p>"));
        window.action(QStringLiteral("toggle-comment"))->trigger();
        QCOMPARE(tab->editor()->toPlainText(), QStringLiteral("<!-- <p>hi</p> -->"));
    }

    void testStatusBarFollowsTheCursor() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        tab->editor()->setPlainText(QStringLiteral("\tabc\nsecond line"));
        tab->goToLine(0, 2);
        QVERIFY2(
            window.statusPositionText().startsWith(QStringLiteral("Ln 1, Col 6")),
            qPrintable(window.statusPositionText())); // the tab counts four columns
        tab->editor()->selectAll();
        QVERIFY(window.statusPositionText().contains(QStringLiteral("Sel 16 | 2")));
    }

    void testFindBarIsPerTab() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        tab->editor()->setPlainText(QStringLiteral("alpha beta alpha"));
        window.action(QStringLiteral("find"))->trigger();
        QVERIFY(tab->findBar()->isVisible());
        tab->findBar()->findField()->setText(QStringLiteral("alpha"));
        QTRY_COMPARE(tab->findBar()->matchCount(), 2);
        window.action(QStringLiteral("find-next"))->trigger();
        QCOMPARE(tab->editor()->textCursor().selectedText(), QStringLiteral("alpha"));
        EditorTab* other = window.newDocument();
        QVERIFY(!other->findBar()->isVisible());
    }

    void testLanguageMenuAndMarkdown() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        tab->editor()->setPlainText(QStringLiteral("# Title\n\nSome *text*."));
        tab->setLanguage(*neko::pad::languageById(u"markdown"));
        QVERIFY(dynamic_cast<neko::editor::MarkdownHighlighter*>(tab->highlighter()));
        QCOMPARE(window.statusLanguageText(), QStringLiteral("Markdown"));
        tab->setLanguage(neko::pad::plainTextLanguage());
        QVERIFY(!tab->highlighter());
        QVERIFY(tab->languageChosen());
    }

    void testHighlighterColoursCode() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral(
            "int x = 42; // note\n"
            "/* block\n"
            "   still */ return \"s\";\n"
            "#define X 1"));
        neko::pad::ui::SyntaxHighlighter highlighter(&doc, *neko::pad::languageById(u"cpp"), false);
        highlighter.rehighlight();
        const auto& f = highlighter.formats();
        QCOMPARE(formatAt(&doc, 0, 0).foreground(), f.type.foreground());     // int
        QCOMPARE(formatAt(&doc, 0, 8).foreground(), f.number.foreground());   // 42
        QCOMPARE(formatAt(&doc, 0, 13).foreground(), f.comment.foreground()); // // note
        QCOMPARE(formatAt(&doc, 1, 0).foreground(), f.comment.foreground());  // /* block
        QCOMPARE(formatAt(&doc, 2, 3).foreground(), f.comment.foreground());  // still */
        QCOMPARE(formatAt(&doc, 2, 12).foreground(), f.keyword.foreground()); // return
        QCOMPARE(formatAt(&doc, 2, 19).foreground(), f.string.foreground());  // "s"
        QCOMPARE(formatAt(&doc, 3, 0).foreground(), f.preprocessor.foreground());
    }

    void testHighlighterMultiLineStringsAndMarkup() {
        QTextDocument py;
        py.setPlainText(QStringLiteral("s = \"\"\"one\ntwo\"\"\"\nx = 1"));
        neko::pad::ui::SyntaxHighlighter pyHighlighter(&py, *neko::pad::languageById(u"python"), true);
        pyHighlighter.rehighlight();
        QCOMPARE(formatAt(&py, 1, 0).foreground(), pyHighlighter.formats().string.foreground());
        QCOMPARE(formatAt(&py, 2, 4).foreground(), pyHighlighter.formats().number.foreground());

        QTextDocument xml;
        xml.setPlainText(QStringLiteral("<a href=\"x\"\n   id='y'>t &amp; u</a>\n<!-- c\n-->"));
        neko::pad::ui::SyntaxHighlighter xmlHighlighter(&xml, *neko::pad::languageById(u"xml"), false);
        xmlHighlighter.rehighlight();
        const auto& f = xmlHighlighter.formats();
        QCOMPARE(formatAt(&xml, 0, 1).foreground(), f.tag.foreground());
        QCOMPARE(formatAt(&xml, 0, 3).foreground(), f.attribute.foreground());
        QCOMPARE(formatAt(&xml, 0, 9).foreground(), f.string.foreground());
        QCOMPARE(formatAt(&xml, 1, 3).foreground(), f.attribute.foreground()); // tag continues
        QCOMPARE(formatAt(&xml, 1, 13).foreground(), f.entity.foreground());
        QCOMPARE(formatAt(&xml, 3, 0).foreground(), f.comment.foreground());
    }

    void testSessionRestoresLazily() {
        const QString a = dir_.filePath(QStringLiteral("session-a.txt"));
        const QString b = dir_.filePath(QStringLiteral("session-b.sql"));
        writeFile(a, "first\nsecond\nthird\n");
        writeFile(b, "select 1;\n");
        {
            MainWindow window;
            window.openFiles({a, b});
            QTRY_COMPARE(window.tabAt(0)->loadState(), EditorTab::LoadState::Ready);
            QTRY_COMPARE(window.tabAt(1)->loadState(), EditorTab::LoadState::Ready);
            window.tabAt(0)->goToLine(2, 1);
            window.tabAt(1)->setLanguage(neko::pad::plainTextLanguage());
            window.tabWidget()->setCurrentIndex(1);
            window.saveSession();
        }
        MainWindow window;
        window.restoreSession();
        QCOMPARE(window.tabCount(), 2);
        QCOMPARE(window.tabWidget()->currentIndex(), 1);
        QTRY_COMPARE(window.tabAt(1)->loadState(), EditorTab::LoadState::Ready);
        QCOMPARE(window.tabAt(1)->language().id, QStringLiteral("text")); // the choice survived
        // The other tab waits until it is shown.
        QCOMPARE(window.tabAt(0)->loadState(), EditorTab::LoadState::Pending);
        window.tabWidget()->setCurrentIndex(0);
        QTRY_COMPARE(window.tabAt(0)->loadState(), EditorTab::LoadState::Ready);
        QCOMPARE(window.tabAt(0)->cursorLineColumn(), std::make_pair(2, 1));
    }

    void testReloadWithEncoding() {
        const QString path = dir_.filePath(QStringLiteral("latin.txt"));
        writeFile(path, "caf\xC3\xA9");
        MainWindow window;
        window.openFiles({path});
        EditorTab* tab = window.currentTab();
        QTRY_COMPARE(tab->loadState(), EditorTab::LoadState::Ready);
        QCOMPARE(tab->editor()->toPlainText(), QStringLiteral("café"));
        tab->reload(neko::pad::Encoding::Latin1);
        QTRY_COMPARE(tab->editor()->toPlainText(), QStringLiteral("cafÃ©"));
        QCOMPARE(tab->encoding(), neko::pad::Encoding::Latin1);
    }

    void testOpeningAMissingFolderEntryFails() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        // A directory is skipped without a tab.
        window.openFiles({dir_.path()});
        QCOMPARE(window.tabCount(), 1);
        QCOMPARE(window.currentTab(), tab);
    }

    void testClosingTheLastTabLeavesAnEmptyOne() {
        MainWindow window;
        EditorTab* tab = window.newDocument();
        QVERIFY(window.closeTab(tab, true));
        QCOMPARE(window.tabCount(), 1);
        QVERIFY(window.currentTab() != tab);
        QVERIFY(window.currentTab()->isUntitled());
    }

    void testSingleInstanceHandsFilesOver() {
        neko::pad::ui::SingleInstance first(QStringLiteral("test-%1").arg(QCoreApplication::applicationPid()));
        QVERIFY(first.listen());
        QSignalSpy received(&first, &neko::pad::ui::SingleInstance::filesReceived);
        neko::pad::ui::SingleInstance second(QStringLiteral("test-%1").arg(QCoreApplication::applicationPid()));
        QVERIFY(!second.listen()); // the name is taken

        // sendToRunning() blocks; the listener needs the event loop. Run it from a thread
        // the way a second process would.
        bool sent = false;
        QThread* thread = QThread::create([&] {
            const neko::pad::ui::SingleInstance client(
                QStringLiteral("test-%1").arg(QCoreApplication::applicationPid()));
            sent = client.sendToRunning({QStringLiteral("/tmp/a.txt"), QStringLiteral("/tmp/b.txt")}, 3000);
        });
        thread->start();
        QTRY_COMPARE(received.count(), 1);
        QVERIFY(thread->wait(5000));
        delete thread;
        QVERIFY(sent);
        QCOMPARE(
            received.front().front().toStringList(),
            (QStringList{QStringLiteral("/tmp/a.txt"), QStringLiteral("/tmp/b.txt")}));
    }

    void testScreenshots() {
        const QByteArray dirName = qgetenv("NEKOPAD_TEST_SCREENSHOTS");
        if (dirName.isEmpty()) {
            QSKIP("NEKOPAD_TEST_SCREENSHOTS is not set");
        }
        const QString path = dir_.filePath(QStringLiteral("shot.cpp"));
        writeFile(
            path,
            "// Neko Pad\n#include <vector>\n\nnamespace neko {\n\n/* The answer. */\nconstexpr int answer() "
            "{\n    return 42;\n}\n\nstd::vector<int> values{1, 2, 3};\nconst char* name = \"Neko\";\n\n} // "
            "namespace neko\n");
        MainWindow window;
        window.openFiles({path});
        window.resize(1000, 640);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(window.currentTab()->loadState(), EditorTab::LoadState::Ready);
        QDir().mkpath(QString::fromLocal8Bit(dirName));
        for (const auto mode : {th::ThemeMode::Light, th::ThemeMode::Dark}) {
            th::ThemeManager::instance().setThemeMode(mode);
            QTest::qWait(100);
            const QString file =
                QDir(QString::fromLocal8Bit(dirName))
                    .filePath(
                        QStringLiteral("nekopad-%1.png").arg(th::ThemeManager::themeModeToString(mode).toLower()));
            QVERIFY(window.grab().save(file));
        }
        th::ThemeManager::instance().setThemeMode(th::ThemeMode::Light);
    }

private:
    QTemporaryDir dir_;
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
