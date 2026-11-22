// How long opening a large file takes, step by step: reading and decoding (worker
// thread), handing the text to the editor, and highlighting. Not a test; run it by hand:
//
//     QT_QPA_PLATFORM=offscreen ./build/bench_load [megabytes]

#include "core/language.hpp"
#include "core/text_file.hpp"
#include "ui/editor_tab.hpp"
#include "ui/syntax_highlighter.hpp"

#include "neko/editor/code_editor.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

namespace {

QByteArray sampleCode(qint64 bytes) {
    static const QByteArray chunk = "// A comment that says what the function does.\n"
                                    "static int compute(const std::vector<int>& values, double factor) {\n"
                                    "    int total = 0; /* a block comment */\n"
                                    "    for (const int v : values) {\n"
                                    "        total += static_cast<int>(v * factor) + 0x2A;\n"
                                    "    }\n"
                                    "    const char* name = \"neko pad\";\n"
                                    "    return total;\n"
                                    "}\n\n";
    QByteArray out;
    out.reserve(bytes + chunk.size());
    while (out.size() < bytes) {
        out += chunk;
    }
    return out;
}

void report(const char* step, qint64 ms) {
    std::printf("  %-34s %6lld ms\n", step, static_cast<long long>(ms));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    const QStringList args = QCoreApplication::arguments();
    const qint64 megabytes = args.size() > 1 ? args[1].toLongLong() : 10;
    const QString path = dir.filePath(QStringLiteral("big.cpp"));
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return 1;
        }
        f.write(sampleCode(megabytes * 1024 * 1024));
    }
    std::printf("Opening a %lld MiB C++ file (%s)\n", static_cast<long long>(megabytes), qPrintable(path));

    QElapsedTimer t;
    t.start();
    const neko::pad::LoadedFile file = neko::pad::loadTextFile(path);
    report("read + detect + decode", t.restart());
    std::printf(
        "  (%lld lines, %s)\n",
        static_cast<long long>(file.decoded.lineCount),
        qPrintable(neko::pad::encodingName(file.decoded.encoding)));

    neko::editor::CodeEditor editor;
    editor.resize(1000, 700);
    editor.show();
    (void)QTest::qWaitForWindowExposed(&editor);
    t.restart();
    editor.setPlainText(file.decoded.text);
    report("CodeEditor::setPlainText", t.restart());
    QCoreApplication::processEvents();
    report("first paint", t.restart());

    {
        neko::pad::ui::SyntaxHighlighter highlighter(editor.document(), *neko::pad::languageById(u"cpp"), false);
        t.restart();
        highlighter.rehighlight();
        report("highlight everything", t.restart());
    }

    // The whole path through the application's tab, as File > Open takes it.
    neko::pad::ui::EditorTab tab;
    tab.resize(1000, 700);
    tab.show();
    QElapsedTimer total;
    total.start();
    t.restart();
    tab.load(path);
    // The longest the event loop was blocked: how unresponsive the window was.
    qint64 longestStall = 0;
    qint64 busy = 0; // time spent handling events: the UI thread's share of the work
    bool firstShown = false;
    while (tab.loadState() != neko::pad::ui::EditorTab::LoadState::Ready) {
        QElapsedTimer stall;
        stall.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        longestStall = std::max(longestStall, stall.elapsed());
        busy += stall.elapsed();
        if (!firstShown && !tab.editor()->document()->isEmpty()) {
            firstShown = true;
            report("EditorTab: first text on screen", t.elapsed());
        }
    }
    QCoreApplication::processEvents();
    report("EditorTab: built, highlighted, shown", total.elapsed());
    report("  longest the window was blocked", longestStall);
    report("  UI thread busy in total", busy);
    std::printf("  (highlighting %s)\n", tab.highlightingSuppressed() ? "off: too large" : "on");
    return 0;
}
