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
#include <QTextBlock>
#include <QTextDocument>
#include <QThreadPool>
#include <QTimer>
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

    connect(editor_->document(), &QTextDocument::modificationChanged, this, &EditorTab::infoChanged);
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
    if (!streamText_.isEmpty()) {
        // Interrupting a file that streams in: undo what streaming changed.
        streamText_.clear();
        streamPos_ = 0;
        editor_->document()->setUndoRedoEnabled(true);
        editor_->setReadOnly(userReadOnly_);
    }
    path_ = path;
    state_ = LoadState::Loading;
    error_.clear();
    const quint64 generation = ++loadGeneration_;
    QPointer<EditorTab> self(this);
    QThreadPool::globalInstance()->start([self, path, forced, generation] {
        LoadedFile file = loadTextFile(path, forced);
        QMetaObject::invokeMethod(
            qApp,
            [self, path, generation, file = std::move(file)] {
                if (self && self->loadGeneration_ == generation) {
                    self->applyLoaded(path, file);
                }
            },
            Qt::QueuedConnection);
    });
    emit infoChanged();
}

void EditorTab::reload(std::optional<Encoding> forced) {
    if (path_.isEmpty()) {
        return;
    }
    pendingCursor_ = cursorLineColumn();
    load(path_, forced);
}

void EditorTab::applyLoaded(const QString& path, const LoadedFile& file) {
    ++loadGeneration_; // a load still running for this tab is outdated now
    path_ = path;
    if (!file.ok) {
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
    if (!languageChosen_) {
        language_ = &detectLanguage(path, firstLineOf(decoded.text));
    }
    delete highlighter_;

    if (decoded.text.size() <= kStreamThreshold) {
        // Small: in one go. The highlighter goes on after the text -- attached first, it
        // would highlight while the text goes in and then once more.
        editor_->setPlainText(decoded.text);
        installHighlighter();
        finishLoading();
        return;
    }

    // Large: the text goes in slice by slice from the event loop, so the first screen
    // shows at once and the window stays responsive. Building the document is what costs
    // (QTextDocument makes a block per line); the highlighter is attached to the empty
    // document, so it colours each slice as it arrives instead of everything at the end.
    state_ = LoadState::Loading;
    userReadOnly_ = editor_->isReadOnly();
    editor_->setReadOnly(true);
    editor_->setPlainText(QString());
    editor_->document()->setUndoRedoEnabled(false);
    installHighlighter();
    streamText_ = decoded.text;
    streamPos_ = 0;
    appendSlice(loadGeneration_);
}

void EditorTab::appendSlice(quint64 generation) {
    if (generation != loadGeneration_) {
        return; // a newer load replaced this one
    }
    // The first slice is small, so it shows at once; the others are large enough that the
    // per-slice overhead does not matter and small enough to keep the window responsive.
    const qsizetype want = streamPos_ == 0 ? kFirstSlice : kSlice;
    qsizetype end = std::min(streamText_.size(), streamPos_ + want);
    if (end < streamText_.size()) {
        // End the slice after a line break, so no line is laid out twice.
        const qsizetype newline = streamText_.indexOf(u'\n', end);
        end = newline < 0 ? streamText_.size() : newline + 1;
    }
    QTextCursor c(editor_->document());
    c.movePosition(QTextCursor::End);
    c.insertText(streamText_.sliced(streamPos_, end - streamPos_));
    streamPos_ = end;
    if (streamPos_ < streamText_.size()) {
        QTimer::singleShot(0, this, [this, generation] { appendSlice(generation); });
        return;
    }
    streamText_.clear();
    streamPos_ = 0;
    editor_->document()->setUndoRedoEnabled(true);
    editor_->setReadOnly(userReadOnly_);
    finishLoading();
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
    // The file's size decides for a file being loaded (the document may still be empty);
    // the document's for one typed or pasted into.
    if (fileSize_ > kHighlightLimit || editor_->document()->characterCount() > kHighlightLimit) {
        highlightSuppressed_ = true;
        return;
    }
    QTextDocument* doc = editor_->document();
    if (language_->family == LanguageFamily::Markdown) {
        highlighter_ = new ed::MarkdownHighlighter(doc, dark_);
    } else {
        highlighter_ = new SyntaxHighlighter(doc, *language_, dark_);
    }
}

bool EditorTab::isModified() const {
    // While a file streams in, the document is being filled, not edited.
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
    QTextDocument* doc = editor_->document();
    QTextOption option = doc->defaultTextOption();
    QTextOption::Flags flags = option.flags();
    flags.setFlag(QTextOption::ShowTabsAndSpaces, spaces);
    flags.setFlag(QTextOption::ShowLineAndParagraphSeparators, lineEndings);
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
