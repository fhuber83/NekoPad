// Tests of nekopad_core, linked alone: encodings, line endings, files, languages, line
// operations.

#include "core/language.hpp"
#include "core/line_ops.hpp"
#include "core/text_encoding.hpp"
#include "core/text_file.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace neko::pad;

class TestCore final : public QObject {
    Q_OBJECT

private slots:
    // ---- Encodings -------------------------------------------------------------------------

    void testUtf8Validation() {
        QVERIFY(isValidUtf8(""));
        QVERIFY(isValidUtf8("plain ASCII text that is longer than eight bytes"));
        QVERIFY(isValidUtf8(
            "Gr\xC3\xBC\xC3\x9F"
            "e \xE2\x82\xAC \xF0\x9F\x90\xB1"));   // Grüße € 🐱
        QVERIFY(!isValidUtf8("\xC3"));             // truncated
        QVERIFY(!isValidUtf8("\xC0\xAF"));         // overlong
        QVERIFY(!isValidUtf8("\xE0\x80\xAF"));     // overlong, three bytes
        QVERIFY(!isValidUtf8("\xED\xA0\x80"));     // a surrogate
        QVERIFY(!isValidUtf8("\xF4\x90\x80\x80")); // past U+10FFFF
        QVERIFY(!isValidUtf8("abcdefgh\xFF"));     // after the fast path
        QVERIFY(!isValidUtf8("caf\xE9"));          // Latin-1
    }

    void testDetectEncoding() {
        QCOMPARE(detectEncoding("hello"), Encoding::Utf8);
        QCOMPARE(detectEncoding("\xEF\xBB\xBFhello"), Encoding::Utf8Bom);
        QCOMPARE(detectEncoding(QByteArrayView("\xFF\xFEh\0", 4)), Encoding::Utf16LeBom);
        QCOMPARE(detectEncoding(QByteArrayView("\xFE\xFF\0h", 4)), Encoding::Utf16BeBom);
        QCOMPARE(detectEncoding("caf\xE9"), Encoding::Latin1);
    }

    void testDecode() {
        const DecodedText utf8 = decode(
            "Gr\xC3\xBC\xC3\x9F"
            "e\r\nzwei\r\n");
        QCOMPARE(utf8.encoding, Encoding::Utf8);
        QCOMPARE(utf8.text, QStringLiteral("Grüße\nzwei\n"));
        QCOMPARE(utf8.eol, Eol::CrLf);
        QVERIFY(!utf8.mixedEols);
        QCOMPARE(utf8.lineCount, 3);

        const DecodedText bom = decode("\xEF\xBB\xBFx");
        QCOMPARE(bom.encoding, Encoding::Utf8Bom);
        QCOMPARE(bom.text, QStringLiteral("x")); // the BOM is not text

        const DecodedText le = decode(QByteArrayView("\xFF\xFEh\0i\0\n\0", 8));
        QCOMPARE(le.encoding, Encoding::Utf16LeBom);
        QCOMPARE(le.text, QStringLiteral("hi\n"));
        QVERIFY(!le.looksBinary);

        const DecodedText latin = decode("caf\xE9");
        QCOMPARE(latin.encoding, Encoding::Latin1);
        QCOMPARE(latin.text, QStringLiteral("café"));

        const DecodedText forced = decode("caf\xC3\xA9", Encoding::Latin1);
        QCOMPARE(forced.text, QStringLiteral("cafÃ©"));

        QVERIFY(decode(QByteArrayView("a\0b", 3)).looksBinary);
    }

    void testEolDetection() {
        QCOMPARE(detectEol(u"a\nb\nc").eol, Eol::Lf);
        QCOMPARE(detectEol(u"a\r\nb").eol, Eol::CrLf);
        QCOMPARE(detectEol(u"a\rb\rc").eol, Eol::Cr);
        QCOMPARE(detectEol(u"no line break").eol, nativeEol());
        const EolStats mixed = detectEol(u"a\r\nb\nc\nd");
        QVERIFY(mixed.mixed);
        QCOMPARE(mixed.eol, Eol::Lf);
        QCOMPARE(mixed.lines, 4);
    }

    void testNormalizeEols() {
        QCOMPARE(normalizeEols(QStringLiteral("a\r\nb\rc\nd\r")), QStringLiteral("a\nb\nc\nd\n"));
        const QString plain = QStringLiteral("no carriage returns\n");
        const QString same = normalizeEols(plain);
        QCOMPARE(same, plain);
        QVERIFY(same.constData() == plain.constData()); // shared, not copied
    }

    void testEncodeRoundTrip_data() {
        QTest::addColumn<int>("encoding");
        QTest::addColumn<int>("eol");
        for (const Encoding e : kAllEncodings) {
            for (const Eol eol : kAllEols) {
                QTest::addRow("%s/%s", qPrintable(encodingId(e)), qPrintable(eolShortName(eol)))
                    << static_cast<int>(e) << static_cast<int>(eol);
            }
        }
    }

    void testEncodeRoundTrip() {
        QFETCH(int, encoding);
        QFETCH(int, eol);
        const auto e = static_cast<Encoding>(encoding);
        const auto lineEnd = static_cast<Eol>(eol);
        const QString text =
            e == Encoding::Latin1 ? QStringLiteral("Grüße\ncafé\n") : QStringLiteral("Grüße € 🐱\nzwei\n");
        const QByteArray bytes = encode(text, e, lineEnd);
        const DecodedText back = decode(bytes);
        QCOMPARE(back.encoding, e);
        QCOMPARE(back.eol, lineEnd);
        QCOMPARE(back.text, text);
    }

    void testCanEncode() {
        QVERIFY(canEncode(u"café", Encoding::Latin1));
        QVERIFY(!canEncode(u"€", Encoding::Latin1));
        QVERIFY(canEncode(u"€", Encoding::Utf8));
    }

    void testEncodingIds() {
        for (const Encoding e : kAllEncodings) {
            QCOMPARE(encodingFromId(encodingId(e)), std::optional<Encoding>(e));
        }
        QVERIFY(!encodingFromId(u"ebcdic"));
    }

    // ---- Files -------------------------------------------------------------------------------

    void testLoadAndSave() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("file.txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(
                "\xEF\xBB\xBF"
                "eins\r\nzwei\r\n");
        }
        const LoadedFile loaded = loadTextFile(path);
        QVERIFY(loaded.ok);
        QCOMPARE(loaded.decoded.encoding, Encoding::Utf8Bom);
        QCOMPARE(loaded.decoded.eol, Eol::CrLf);
        QCOMPARE(loaded.decoded.text, QStringLiteral("eins\nzwei\n"));
        QCOMPARE(loaded.size, 15);

        const SaveResult saved = saveTextFile(path, u"drei\nvier\n", Encoding::Utf8Bom, Eol::CrLf);
        QVERIFY(saved.ok);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(
            f.readAll(),
            QByteArray(
                "\xEF\xBB\xBF"
                "drei\r\nvier\r\n"));
    }

    void testLoadEmptyAndMissing() {
        QTemporaryDir dir;
        const QString empty = dir.filePath(QStringLiteral("empty.txt"));
        {
            QFile f(empty);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const LoadedFile e = loadTextFile(empty);
        QVERIFY(e.ok);
        QVERIFY(e.decoded.text.isEmpty());

        const LoadedFile missing = loadTextFile(dir.filePath(QStringLiteral("missing.txt")));
        QVERIFY(!missing.ok);
        QVERIFY(!missing.error.isEmpty());
    }

    void testFormatByteSize() {
        QCOMPARE(formatByteSize(512), QStringLiteral("512 B"));
        QCOMPARE(formatByteSize(2048), QStringLiteral("2.0 KiB"));
        QCOMPARE(formatByteSize(5LL * 1024 * 1024), QStringLiteral("5.0 MiB"));
    }

    // ---- Languages ---------------------------------------------------------------------------

    void testLanguageTable() {
        const auto all = languages();
        QVERIFY(all.size() > 25);
        QCOMPARE(all.front().id, QStringLiteral("text"));
        QSet<QString> ids;
        for (const Language& l : all) {
            QVERIFY2(!ids.contains(l.id), qPrintable(l.id));
            ids.insert(l.id);
            QVERIFY(std::ranges::is_sorted(l.keywords));
            if (!l.blockStart.isEmpty()) {
                QVERIFY2(!l.blockEnd.isEmpty(), qPrintable(l.id));
            }
        }
    }

    void testLanguageForFileName() {
        auto id = [](const char* name) {
            const Language* l = languageForFileName(QString::fromLatin1(name));
            return l ? l->id : QString();
        };
        QCOMPARE(id("main.cpp"), QStringLiteral("cpp"));
        QCOMPARE(id("/src/WIDGET.HPP"), QStringLiteral("cpp"));
        QCOMPARE(id("C:\\src\\x.py"), QStringLiteral("python"));
        QCOMPARE(id("CMakeLists.txt"), QStringLiteral("cmake")); // by name, before ".txt"
        QCOMPARE(id("notes.txt"), QStringLiteral("text"));
        QCOMPARE(id("Makefile"), QStringLiteral("makefile"));
        QCOMPARE(id("config.yml"), QStringLiteral("yaml"));
        QCOMPARE(id("index.html"), QStringLiteral("html"));
        QCOMPARE(id("README.md"), QStringLiteral("markdown"));
        QCOMPARE(id("no-extension"), QString());
        QCOMPARE(id("trailing."), QString());
    }

    void testLanguageForFirstLine() {
        auto id = [](const char* line) {
            const Language* l = languageForFirstLine(QString::fromLatin1(line));
            return l ? l->id : QString();
        };
        QCOMPARE(id("#!/bin/bash"), QStringLiteral("shell"));
        QCOMPARE(id("#!/usr/bin/env python3"), QStringLiteral("python"));
        QCOMPARE(id("#!/usr/bin/env -S python3.12 -u"), QStringLiteral("python"));
        QCOMPARE(id("#!/usr/bin/node"), QStringLiteral("javascript"));
        QCOMPARE(id("<?xml version=\"1.0\"?>"), QStringLiteral("xml"));
        QCOMPARE(id("<!DOCTYPE html>"), QStringLiteral("html"));
        QCOMPARE(id("diff --git a/x b/x"), QStringLiteral("diff"));
        QCOMPARE(id("just text"), QString());
        QCOMPARE(detectLanguage(u"script", u"#!/bin/sh").id, QStringLiteral("shell"));
        QCOMPARE(detectLanguage(u"x.unknown", u"").id, QStringLiteral("text"));
    }

    void testContainsWord() {
        const Language* cpp = languageById(u"cpp");
        QVERIFY(cpp);
        QVERIFY(containsWord(cpp->keywords, u"constexpr", false));
        QVERIFY(!containsWord(cpp->keywords, u"Constexpr", false));
        const Language* sql = languageById(u"sql");
        QVERIFY(containsWord(sql->keywords, u"SELECT", true));
        QVERIFY(containsWord(sql->keywords, u"select", true));
        QVERIFY(!containsWord(sql->keywords, u"selection", true));
    }

    void testFileDialogFilter() {
        const QString filter = fileDialogFilter();
        QVERIFY(filter.startsWith(QStringLiteral("All files (*)")));
        QVERIFY(filter.contains(QStringLiteral("*.cpp")));
    }

    // ---- Line operations ---------------------------------------------------------------------

    void testSortAndDeduplicate() {
        const QStringList in{QStringLiteral("b"), QStringLiteral("A"), QStringLiteral("a"), QStringLiteral("b")};
        QCOMPARE(
            lines::sorted(in, true, Qt::CaseSensitive),
            (QStringList{QStringLiteral("A"), QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("b")}));
        QCOMPARE(lines::sorted(in, false, Qt::CaseSensitive).front(), QStringLiteral("b"));
        QCOMPARE(
            lines::withoutDuplicates(in), (QStringList{QStringLiteral("b"), QStringLiteral("A"), QStringLiteral("a")}));
    }

    void testEmptyAndTrim() {
        const QStringList in{QStringLiteral("a  "), QString(), QStringLiteral("   "), QStringLiteral("\tb")};
        QCOMPARE(lines::withoutEmpty(in, false).size(), 3);
        QCOMPARE(lines::withoutEmpty(in, true).size(), 2);
        QCOMPARE(lines::trimmedTrailing(in).front(), QStringLiteral("a"));
        QCOMPARE(lines::trimmedLeading(in).back(), QStringLiteral("b"));
    }

    void testToggleLineComment() {
        const QStringList code{QStringLiteral("    int a;"), QString(), QStringLiteral("  int b;")};
        const QStringList on = lines::toggledLineComment(code, QStringLiteral("//"));
        QCOMPARE(on, (QStringList{QStringLiteral("  //   int a;"), QString(), QStringLiteral("  // int b;")}));
        QCOMPARE(lines::toggledLineComment(on, QStringLiteral("//")), code);

        // Mixed: some commented, some not -> comment all.
        const QStringList mixed{QStringLiteral("# a"), QStringLiteral("b")};
        QCOMPARE(
            lines::toggledLineComment(mixed, QStringLiteral("#")),
            (QStringList{QStringLiteral("# # a"), QStringLiteral("# b")}));
    }

    void testToggleBlockComment() {
        const QStringList html{QStringLiteral("  <p>x</p>")};
        const QStringList on = lines::toggledBlockComment(html, QStringLiteral("<!--"), QStringLiteral("-->"));
        QCOMPARE(on, QStringList{QStringLiteral("  <!-- <p>x</p> -->")});
        QCOMPARE(lines::toggledBlockComment(on, QStringLiteral("<!--"), QStringLiteral("-->")), html);
    }

    void testIndentation() {
        const QStringList tabs{QStringLiteral("\t\tx"), QStringLiteral(" \ty")};
        QCOMPARE(
            lines::indentationToSpaces(tabs, 4), (QStringList{QStringLiteral("        x"), QStringLiteral("    y")}));
        QCOMPARE(lines::indentationToTabs({QStringLiteral("      z")}, 4), QStringList{QStringLiteral("\t  z")});
    }

    void testJoinAndColumn() {
        QCOMPARE(lines::joined({QStringLiteral(" a "), QString(), QStringLiteral("b")}), QStringLiteral("a b"));
        QCOMPARE(lines::displayColumn(u"\tab", 1, 4), 4);
        QCOMPARE(lines::displayColumn(u"ab\tc", 3, 4), 4);
        QCOMPARE(lines::displayColumn(u"abc", 2, 4), 2);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
