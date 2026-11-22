#include "ui/editor_tab.hpp"

#include "core/line_ops.hpp"
#include "ui/syntax_highlighter.hpp"

#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_defaults.hpp"
#include "neko/editor/editor_find_bar.hpp"
#include "neko/editor/markdown_highlighter.hpp"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPlainTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

namespace neko::pad::ui {

namespace ed = neko::editor;

namespace {

/// The document's text with `\n` line breaks, exactly as typed. Unlike
/// QPlainTextEdit::toPlainText() this keeps non-breaking spaces (toPlainText turns
/// them into plain spaces, which would change the file on save).
[[nodiscard]] QString documentText(const QTextDocument* document) {
    QString text = document->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

[[nodiscard]] QString selectionText(const QTextCursor& cursor) {
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

[[nodiscard]] QStringView firstLineOf(const QString& text) {
    qsizetype end = text.indexOf(u'\n');
    if (end < 0) {
        end = text.size();
    }
    return QStringView(text).first(std::min<qsizetype>(end, 512));
}

[[nodiscard]] bool allLocalFiles(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    const QList<QUrl> urls = mime->urls();
    return !urls.isEmpty() && std::ranges::all_of(urls, [](const QUrl& u) { return u.isLocalFile(); });
}

/// Whether `language` is highlighted at this size.
[[nodiscard]] bool highlightAllowed(const Language& language, qint64 fileSize, qsizetype characters) {
    return language.family != LanguageFamily::Plain && fileSize <= EditorTab::kHighlightLimit &&
           characters <= EditorTab::kHighlightLimit;
}

/// A highlighter for `language` on `document`, as the document's child; nullptr for plain
/// text. Any thread: the one the document lives in.
QSyntaxHighlighter* makeHighlighter(QTextDocument* document, const Language& language, bool dark) {
    switch (language.family) {
    case LanguageFamily::Plain:
        return nullptr;
    case LanguageFamily::Markdown:
        return new ed::MarkdownHighlighter(document, dark);
    default:
        return new SyntaxHighlighter(document, language, dark);
    }
}

/// The start of `text` up to about `limit` characters, ending after a line break.
[[nodiscard]] QString previewOf(const QString& text, qsizetype limit) {
    const qsizetype newline = text.indexOf(u'\n', std::min(limit, text.size()));
    return newline < 0 ? text : text.first(newline + 1);
}

} // namespace

EditorTab::EditorTab(QWidget* parent)
    : QWidget(parent),
      language_(&plainTextLanguage()) {
    editor_ = new ed::CodeEditor(this);
    editor_->setFrameShape(QFrame::NoFrame);
    findBar_ = new ed::EditorFindBar(
        editor_, [e = editor_](const QList<QTextEdit::ExtraSelection>& h) { e->setSearchHighlights(h); }, this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(editor_, 1);
    layout->addWidget(findBar_);

    setFocusProxy(editor_);
    editor_->viewport()->installEventFilter(this);

    modificationConnection_ =
        connect(editor_->document(), &QTextDocument::modificationChanged, this, &EditorTab::infoChanged);
    connect(editor_, &ed::CodeEditor::documentReplaced, this, &EditorTab::documentReplaced);
    // CodeEditor takes EditorDefaults' font when it changes; keep the zoom on top of it.
    connect(&ed::EditorDefaults::instance(), &ed::EditorDefaults::fontChanged, this, [this] { setZoom(zoom_); });
}

EditorTab::~EditorTab() = default;

QString EditorTab::displayName() const {
    return path_.isEmpty() ? untitledName_ : QFileInfo(path_).fileName();
}

void EditorTab::setUntitledName(const QString& name) {
    untitledName_ = name;
    emit infoChanged();
}

void EditorTab::setPendingFile(const QString& path) {
    path_ = path;
    state_ = LoadState::Pending;
    if (!languageChosen_) {
        language_ = &detectLanguage(path, {});
    }
    emit infoChanged();
}

void EditorTab::ensureLoaded() {
    if (state_ == LoadState::Pending) {
        load(path_);
    }
}

void EditorTab::load(const QString& path, std::optional<Encoding> forced) {
    endPreview();
    path_ = path;
    state_ = LoadState::Loading;
    error_.clear();
    const quint64 generation = ++loadGeneration_;
    QPointer<EditorTab> self(this);
    // What the worker needs to know of this tab, copied: it must not touch the tab.
    // The editor's font and text options (tabs, wrapping, visible whitespace): built with
    // them, the document is laid out once when the editor adopts it, not three times.
    const QFont font = editor_->font();
    const QTextOption options = editor_->document()->defaultTextOption();
    const bool dark = dark_;
    const Language* chosen = languageChosen_ ? language_ : nullptr;
    QThread* const uiThread = thread();
    QThreadPool::globalInstance()->start([=] {
        Prepared p;
        p.file = loadTextFile(path, forced);
        p.dark = dark;
        const QString& text = p.file.decoded.text;
        if (p.file.ok && text.size() > kPrebuildThreshold) {
            // Something to look at while the document is built.
            QMetaObject::invokeMethod(
                qApp,
                [self, generation, preview = previewOf(text, kPreviewChars)] {
                    if (self && self->loadGeneration_ == generation) {
                        self->showPreview(preview);
                    }
                },
                Qt::QueuedConnection);

            // The expensive part, here rather than on the UI thread: a QTextDocument may be
            // built in any thread, and so may its highlighter run (block formats only).
            p.language = chosen ? chosen : &detectLanguage(path, firstLineOf(text));
            auto* doc = new QTextDocument;
            doc->setDocumentLayout(new QPlainTextDocumentLayout(doc));
            doc->setDefaultFont(font);
            doc->setDefaultTextOption(options);
            doc->setPlainText(text);
            if (highlightAllowed(*p.language, p.file.size, text.size())) {
                if (QSyntaxHighlighter* h = makeHighlighter(doc, *p.language, dark)) {
                    h->rehighlight();
                }
            } else {
                p.highlightSuppressed = p.language->family != LanguageFamily::Plain;
            }
            doc->setModified(false);
            doc->moveToThread(uiThread); // with its children: layout and highlighter
            p.document = doc;
            p.file.decoded.text = QString(); // the document holds it now
        }
        QMetaObject::invokeMethod(
            qApp,
            [self, path, generation, p] {
                if (self && self->loadGeneration_ == generation) {
                    self->applyLoaded(path, p);
                } else {
                    delete p.document; // replaced by a newer load, or the tab is gone
                }
            },
            Qt::QueuedConnection);
    });
    emit infoChanged();
}

void EditorTab::showPreview(const QString& text) {
    if (state_ != LoadState::Loading || !editor_->document()->isEmpty()) {
        return; // a reload keeps showing the old text until the new one is ready
    }
    previewing_ = true;
    userReadOnly_ = editor_->isReadOnly();
    editor_->setReadOnly(true);
    editor_->setPlainText(text);
}

void EditorTab::endPreview() {
    if (previewing_) {
        previewing_ = false;
        editor_->setReadOnly(userReadOnly_);
    }
}

void EditorTab::reload(std::optional<Encoding> forced) {
    if (path_.isEmpty()) {
        return;
    }
    pendingCursor_ = cursorLineColumn();
    load(path_, forced);
}

void EditorTab::applyLoaded(const QString& path, const Prepared& prepared) {
    ++loadGeneration_; // a load still running for this tab is outdated now
    endPreview();
    path_ = path;
    const LoadedFile& file = prepared.file;
    if (!file.ok) {
        delete prepared.document;
        state_ = LoadState::Failed;
        error_ = file.error;
        emit loaded();
        emit infoChanged();
        return;
    }
    error_.clear();
    const DecodedText& decoded = file.decoded;
    encoding_ = decoded.encoding;
    eol_ = decoded.eol;
    mixedEols_ = decoded.mixedEols;
    looksBinary_ = decoded.looksBinary;
    fileSize_ = file.size;
    knownModified_ = file.modified;
    delete highlighter_;

    if (QTextDocument* doc = prepared.document) {
        // Built and highlighted on the worker: adopting it is all that is left. As the
        // editor's child, the editor deletes it when the next one replaces it.
        if (!languageChosen_ && prepared.language) {
            language_ = prepared.language;
        }
        doc->setParent(editor_);
        editor_->replaceDocument(doc);
        highlighter_ = doc->findChild<QSyntaxHighlighter*>();
        highlightSuppressed_ = prepared.highlightSuppressed;
        if (prepared.dark != dark_) {
            // The theme changed while the worker coloured it: recolour.
            const bool wanted = dark_;
            dark_ = prepared.dark;
            setDark(wanted);
        }
        finishLoading();
        return;
    }

    // Small: in one go. The highlighter goes on after the text -- attached first, it would
    // highlight while the text goes in and then once more.
    if (!languageChosen_) {
        language_ = &detectLanguage(path, firstLineOf(decoded.text));
    }
    editor_->setPlainText(decoded.text);
    installHighlighter();
    finishLoading();
}

void EditorTab::documentReplaced() {
    disconnect(modificationConnection_);
    modificationConnection_ =
        connect(editor_->document(), &QTextDocument::modificationChanged, this, &EditorTab::infoChanged);
    applyTextOptions();
}

void EditorTab::finishLoading() {
    state_ = LoadState::Ready;
    editor_->document()->setModified(false);
    if (pendingCursor_) {
        goToLine(pendingCursor_->first, pendingCursor_->second);
        pendingCursor_.reset();
    }
    emit loaded();
    emit infoChanged();
}

SaveResult EditorTab::save() {
    return saveAs(path_);
}

SaveResult EditorTab::saveAs(const QString& path) {
    const SaveResult result = saveTextFile(path, documentText(editor_->document()), encoding_, eol_);
    if (!result.ok) {
        return result;
    }
    const bool renamed = path != path_;
    path_ = path;
    knownModified_ = result.modified;
    fileSize_ = QFileInfo(path).size();
    mixedEols_ = false;
    state_ = LoadState::Ready;
    if (renamed && !languageChosen_) {
        const Language& detected = detectLanguage(path, firstLineOf(editor_->document()->firstBlock().text()));
        if (&detected != language_) {
            language_ = &detected;
            delete highlighter_;
            installHighlighter();
        }
    }
    editor_->document()->setModified(false);
    emit infoChanged();
    return result;
}

void EditorTab::setEncoding(Encoding encoding) {
    if (encoding == encoding_) {
        return;
    }
    encoding_ = encoding;
    editor_->document()->setModified(true);
    emit infoChanged();
}

void EditorTab::setEol(Eol eol) {
    if (eol == eol_ && !mixedEols_) {
        return;
    }
    eol_ = eol;
    mixedEols_ = false;
    editor_->document()->setModified(true);
    emit infoChanged();
}

void EditorTab::setLanguage(const Language& language) {
    languageChosen_ = true;
    if (&language == language_ && (highlighter_ || highlightSuppressed_ || language.family == LanguageFamily::Plain)) {
        return;
    }
    language_ = &language;
    delete highlighter_;
    installHighlighter();
    emit infoChanged();
}

void EditorTab::installHighlighter() {
    highlightSuppressed_ = false;
    if (language_->family == LanguageFamily::Plain) {
        return;
    }
    // The file's size decides for a file being loaded; the document's for one typed or
    // pasted into.
    if (!highlightAllowed(*language_, fileSize_, editor_->document()->characterCount())) {
        highlightSuppressed_ = true;
        return;
    }
    highlighter_ = makeHighlighter(editor_->document(), *language_, dark_);
}

bool EditorTab::isModified() const {
    // While a file loads, the editor shows a preview, not an edit.
    return state_ == LoadState::Ready && editor_->document()->isModified();
}

bool EditorTab::isReadOnly() const {
    return editor_->isReadOnly();
}

void EditorTab::setReadOnly(bool readOnly) {
    editor_->setReadOnly(readOnly);
    emit infoChanged();
}

void EditorTab::setDark(bool dark) {
    if (dark == dark_) {
        return;
    }
    dark_ = dark;
    if (auto* md = dynamic_cast<ed::MarkdownHighlighter*>(highlighter_.data())) {
        md->setDark(dark);
    } else if (auto* code = dynamic_cast<SyntaxHighlighter*>(highlighter_.data())) {
        code->setDark(dark);
    }
}

void EditorTab::setShowWhitespace(bool spaces, bool lineEndings) {
    showSpaces_ = spaces;
    showEols_ = lineEndings;
    applyTextOptions();
}

void EditorTab::applyTextOptions() {
    QTextDocument* doc = editor_->document();
    QTextOption option = doc->defaultTextOption();
    QTextOption::Flags flags = option.flags();
    flags.setFlag(QTextOption::ShowTabsAndSpaces, showSpaces_);
    flags.setFlag(QTextOption::ShowLineAndParagraphSeparators, showEols_);
    if (flags != option.flags()) {
        option.setFlags(flags);
        doc->setDefaultTextOption(option);
    }
}

void EditorTab::setZoom(int points) {
    zoom_ = points;
    QFont font = ed::EditorDefaults::instance().font();
    if (points != 0) {
        const qreal base = font.pointSizeF() > 0 ? font.pointSizeF() : 11.0;
        font.setPointSizeF(std::max<qreal>(4.0, base + points));
    }
    editor_->setFont(font);
}

std::pair<int, int> EditorTab::cursorLineColumn() const {
    const QTextCursor c = editor_->textCursor();
    return {c.blockNumber(), c.positionInBlock()};
}

std::pair<int, int> EditorTab::sessionCursor() const {
    if (state_ != LoadState::Ready && pendingCursor_) {
        return *pendingCursor_;
    }
    return cursorLineColumn();
}

void EditorTab::goToLine(int line, int column) {
    if (state_ != LoadState::Ready) {
        pendingCursor_ = {line, column};
        return;
    }
    QTextDocument* doc = editor_->document();
    const QTextBlock block = doc->findBlockByNumber(std::clamp(line, 0, doc->blockCount() - 1));
    QTextCursor c(block);
    c.setPosition(block.position() + std::clamp(column, 0, std::max(0, block.length() - 1)));
    editor_->setTextCursor(c);
    editor_->centerCursor();
}

std::pair<int, int> EditorTab::selectedLineRange() const {
    const QTextCursor c = editor_->textCursor();
    const QTextDocument* doc = editor_->document();
    const QTextBlock first = doc->findBlock(c.selectionStart());
    QTextBlock last = doc->findBlock(c.selectionEnd());
    if (c.hasSelection() && last.position() == c.selectionEnd() && last.blockNumber() > first.blockNumber()) {
        last = last.previous();
    }
    return {first.blockNumber(), last.blockNumber()};
}

void EditorTab::transformSelectedLines(const std::function<QStringList(QStringList)>& op) {
    editor_->clearBlockSelection();
    const auto [first, last] = selectedLineRange();
    QTextDocument* doc = editor_->document();
    const QTextBlock b1 = doc->findBlockByNumber(first);
    const QTextBlock b2 = doc->findBlockByNumber(last);
    const int start = b1.position();
    QTextCursor c(doc);
    c.setPosition(start);
    c.setPosition(b2.position() + b2.length() - 1, QTextCursor::KeepAnchor);
    const QString before = selectionText(c);
    const QString after = op(before.split(QLatin1Char('\n'))).join(QLatin1Char('\n'));
    if (after == before) {
        return;
    }
    c.beginEditBlock();
    c.insertText(after);
    c.endEditBlock();
    c.setPosition(start);
    c.setPosition(start + static_cast<int>(after.size()), QTextCursor::KeepAnchor);
    editor_->setTextCursor(c);
}

void EditorTab::transformSelection(const std::function<QString(const QString&)>& op) {
    editor_->clearBlockSelection();
    QTextCursor c = editor_->textCursor();
    if (!c.hasSelection()) {
        // Without a selection: the word under the cursor, as Notepad++ does.
        c.select(QTextCursor::WordUnderCursor);
        if (!c.hasSelection()) {
            return;
        }
    }
    const int start = c.selectionStart();
    const QString before = selectionText(c);
    const QString after = op(before);
    if (after == before) {
        return;
    }
    c.beginEditBlock();
    c.insertText(after);
    c.endEditBlock();
    c.setPosition(start);
    c.setPosition(start + static_cast<int>(after.size()), QTextCursor::KeepAnchor);
    editor_->setTextCursor(c);
}

void EditorTab::duplicateLines() {
    editor_->clearBlockSelection();
    QTextCursor c = editor_->textCursor();
    const QString selected = selectionText(c);
    if (c.hasSelection() && !selected.contains(QLatin1Char('\n'))) {
        // A selection within a line: duplicate just that.
        const int end = c.selectionEnd();
        c.beginEditBlock();
        c.setPosition(end);
        c.insertText(selected);
        c.endEditBlock();
        return;
    }
    const auto [first, last] = selectedLineRange();
    QTextDocument* doc = editor_->document();
    const QTextBlock b1 = doc->findBlockByNumber(first);
    const QTextBlock b2 = doc->findBlockByNumber(last);
    QTextCursor range(doc);
    range.setPosition(b1.position());
    range.setPosition(b2.position() + b2.length() - 1, QTextCursor::KeepAnchor);
    const QString text = selectionText(range);
    QTextCursor insert(doc);
    insert.setPosition(b2.position() + b2.length() - 1);
    insert.beginEditBlock();
    insert.insertText(QLatin1Char('\n') + text);
    insert.endEditBlock();
}

void EditorTab::deleteLines() {
    editor_->clearBlockSelection();
    const auto [first, last] = selectedLineRange();
    QTextDocument* doc = editor_->document();
    const QTextBlock b1 = doc->findBlockByNumber(first);
    const QTextBlock b2 = doc->findBlockByNumber(last);
    QTextCursor c(doc);
    if (b2.next().isValid()) {
        c.setPosition(b1.position());
        c.setPosition(b2.next().position(), QTextCursor::KeepAnchor);
    } else if (b1.previous().isValid()) {
        // The last line: take the line break before it instead.
        c.setPosition(b1.position() - 1);
        c.setPosition(b2.position() + b2.length() - 1, QTextCursor::KeepAnchor);
    } else {
        c.select(QTextCursor::Document);
    }
    c.beginEditBlock();
    c.removeSelectedText();
    c.endEditBlock();
    editor_->setTextCursor(c);
}

void EditorTab::moveLines(bool up) {
    editor_->clearBlockSelection();
    const auto [first, last] = selectedLineRange();
    QTextDocument* doc = editor_->document();
    if ((up && first == 0) || (!up && last >= doc->blockCount() - 1)) {
        return;
    }
    const QTextBlock b1 = doc->findBlockByNumber(first);
    const QTextBlock b2 = doc->findBlockByNumber(last);
    const QTextCursor old = editor_->textCursor();
    const int anchorOffset = old.anchor() - b1.position();
    const int positionOffset = old.position() - b1.position();

    QTextCursor range(doc);
    range.setPosition(b1.position());
    range.setPosition(b2.position() + b2.length() - 1, QTextCursor::KeepAnchor);
    const QString moving = selectionText(range);

    QTextCursor c(doc);
    QString replacement;
    int newStart = 0;
    if (up) {
        const QTextBlock other = b1.previous();
        c.setPosition(other.position());
        c.setPosition(b2.position() + b2.length() - 1, QTextCursor::KeepAnchor);
        replacement = moving + QLatin1Char('\n') + other.text();
        newStart = other.position();
    } else {
        const QTextBlock other = b2.next();
        c.setPosition(b1.position());
        c.setPosition(other.position() + other.length() - 1, QTextCursor::KeepAnchor);
        replacement = other.text() + QLatin1Char('\n') + moving;
        newStart = b1.position() + other.length();
    }
    c.beginEditBlock();
    c.insertText(replacement);
    c.endEditBlock();
    QTextCursor after(doc);
    after.setPosition(newStart + anchorOffset);
    after.setPosition(newStart + positionOffset, QTextCursor::KeepAnchor);
    editor_->setTextCursor(after);
}

void EditorTab::toggleComment() {
    const Language& l = *language_;
    if (!l.lineComment.isEmpty()) {
        const QString token = l.lineComment;
        transformSelectedLines(
            [token](QStringList lines) { return lines::toggledLineComment(std::move(lines), token); });
    } else if (!l.blockStart.isEmpty()) {
        const QString start = l.blockStart;
        const QString end = l.blockEnd;
        transformSelectedLines(
            [start, end](QStringList lines) { return lines::toggledBlockComment(std::move(lines), start, end); });
    }
}

bool EditorTab::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor_->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            auto* drag = static_cast<QDragMoveEvent*>(event);
            if (allLocalFiles(drag->mimeData())) {
                drag->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto* drop = static_cast<QDropEvent*>(event);
            if (allLocalFiles(drop->mimeData())) {
                drop->acceptProposedAction();
                emit filesDropped(drop->mimeData()->urls());
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace neko::pad::ui
