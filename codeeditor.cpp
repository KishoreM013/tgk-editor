#include "codeeditor.h"

#include <QPainter>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged,
            this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest,
            this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged,
            this, &CodeEditor::highlightCurrentLine);

    m_completionTimer = new QTimer(this);
    m_completionTimer->setSingleShot(true);
    m_completionTimer->setInterval(250);
    connect(m_completionTimer, &QTimer::timeout,
            this, &CodeEditor::completionRequested);

    viewport()->setMouseTracking(true); // needed for diagnostic tooltips
    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    setLineWrapMode(QPlainTextEdit::NoWrap);
}

void CodeEditor::setCompleter(QCompleter* c) {
    if (m_completer)
        m_completer->disconnect(this);
    m_completer = c;
    if (!m_completer)
        return;
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    // The Ide connects to QCompleter::activated itself to insert text.
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(),
                                 rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(
        QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::setTabWidth(int width, bool useTabs) {
    m_tabWidth = qBound(1, width, 16);
    m_useTabs = useTabs;
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) *
                       m_tabWidth);
}

QString CodeEditor::indentUnit() const {
    return m_useTabs ? QString('\t') : QString(m_tabWidth, ' ');
}

void CodeEditor::hideCompletionPopup() {
    if (m_completer && m_completer->popup())
        m_completer->popup()->hide();
}

// Tab / Shift+Tab: indent or outdent the selected lines (or the current one).
void CodeEditor::indentSelection(bool outdent) {
    QTextCursor cur = textCursor();
    const int startBlock =
        document()->findBlock(cur.hasSelection() ? cur.selectionStart()
                                                 : cur.position())
            .blockNumber();
    int endBlock =
        document()->findBlock(cur.hasSelection() ? cur.selectionEnd()
                                                 : cur.position())
            .blockNumber();
    // A selection ending at column 0 does not include that last line.
    if (cur.hasSelection() && endBlock > startBlock &&
        cur.selectionEnd() ==
            document()->findBlock(cur.selectionEnd()).position())
        --endBlock;

    const QString unit = indentUnit();
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (int n = startBlock; n <= endBlock; ++n) {
        QTextBlock block = document()->findBlockByNumber(n);
        if (!block.isValid())
            continue;
        QTextCursor c(block);
        if (outdent) {
            const QString text = block.text();
            int remove = 0;
            if (!text.isEmpty() && text.at(0) == '\t') {
                remove = 1;
            } else {
                while (remove < m_tabWidth && remove < text.size() &&
                       text.at(remove) == ' ')
                    ++remove;
            }
            if (remove > 0) {
                c.setPosition(block.position() + remove,
                              QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else {
            c.insertText(unit);
        }
    }
    edit.endEditBlock();
    hideCompletionPopup();
}

// Enter: keep the current indentation, add one level after { [ ( or the
// Python ':' and expand "{ }" into three properly aligned lines.
void CodeEditor::insertNewlineWithIndent() {
    hideCompletionPopup();
    QTextCursor cur = textCursor();
    if (cur.hasSelection())
        cur.removeSelectedText();

    const QTextBlock block = cur.block();
    const QString text = block.text();
    const int col = cur.positionInBlock();

    QString indent;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == ' ' || ch == '\t')
            indent += ch;
        else
            break;
    }

    const QString before = text.left(col).trimmed();
    const QChar last = before.isEmpty() ? QChar() : before.at(before.size() - 1);
    const QChar next = (col < text.size()) ? text.at(col) : QChar();

    auto matching = [](QChar o, QChar c) {
        return (o == '{' && c == '}') || (o == '[' && c == ']') ||
               (o == '(' && c == ')');
    };
    const bool between = matching(last, next);

    QTextCursor c = cur;
    c.beginEditBlock();
    if (between) {
        // "{|}" -> opening line, indented empty line, closing line.
        c.insertText("\n" + indent + indentUnit() + "\n" + indent);
        c.movePosition(QTextCursor::Up);
        c.movePosition(QTextCursor::EndOfLine);
    } else {
        QString newIndent = indent;
        if (last == '{' || last == '[' || last == '(' || last == ':')
            newIndent += indentUnit();
        c.insertText("\n" + newIndent);
    }
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
}

// Auto-close (, [, { and quotes. Returns false to let the char be typed
// normally (e.g. an apostrophe inside a word).
bool CodeEditor::insertPair(QChar open) {
    QTextCursor cur = textCursor();
    const int col = cur.positionInBlock();

    if (open == '"' || open == '\'' || open == '`') {
        if (!cur.hasSelection() && col > 0) {
            const QChar prev = cur.block().text().at(col - 1);
            if (prev.isLetterOrNumber() || prev == '_')
                return false; // it's an apostrophe, not a string
        }
    }

    QChar close = open;
    if (open == '(')
        close = ')';
    else if (open == '[')
        close = ']';
    else if (open == '{')
        close = '}';

    QTextCursor c = cur;
    c.beginEditBlock();
    if (c.hasSelection()) {
        QString sel = c.selectedText();
        sel.replace(QChar::ParagraphSeparator, '\n');
        c.insertText(QString(open) + sel + QString(close));
    } else {
        c.insertText(QString(open) + QString(close));
        c.movePosition(QTextCursor::Left); // cursor between the pair
    }
    c.endEditBlock();
    setTextCursor(c);
    hideCompletionPopup();
    return true;
}

// Typing the closing char when it is already there just steps over it.
bool CodeEditor::skipClosing(QChar close) {
    QTextCursor cur = textCursor();
    if (cur.hasSelection())
        return false;
    const QString text = cur.block().text();
    const int col = cur.positionInBlock();
    if (col >= text.size() || text.at(col) != close)
        return false;
    QTextCursor c = cur;
    c.movePosition(QTextCursor::Right);
    setTextCursor(c);
    hideCompletionPopup();
    return true;
}

// Backspace between an empty pair removes both characters.
bool CodeEditor::deleteBracketPair() {
    QTextCursor cur = textCursor();
    if (cur.hasSelection())
        return false;
    const int pos = cur.position();
    const QString text = toPlainText();
    if (pos <= 0 || pos >= text.size())
        return false;
    const QChar before = text.at(pos - 1);
    const QChar after = text.at(pos);
    const bool pair = (before == '(' && after == ')') ||
                      (before == '[' && after == ']') ||
                      (before == '{' && after == '}') ||
                      (before == '"' && after == '"') ||
                      (before == '\'' && after == '\'') ||
                      (before == '`' && after == '`');
    if (!pair)
        return false;

    QTextCursor c = cur;
    c.beginEditBlock();
    c.setPosition(pos - 1);
    c.setPosition(pos + 1, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.endEditBlock();
    setTextCursor(c);
    hideCompletionPopup();
    return true;
}

// Typing } ) ] on an otherwise empty, indented line first removes one level.
bool CodeEditor::dedentIfOnlyWhitespace() {
    QTextCursor cur = textCursor();
    if (cur.hasSelection())
        return false;
    const QTextBlock block = cur.block();
    const QString text = block.text();
    if (!text.trimmed().isEmpty() || cur.positionInBlock() != text.size())
        return false;

    int remove = 0;
    if (!text.isEmpty() && text.at(0) == '\t') {
        remove = 1;
    } else {
        while (remove < m_tabWidth && remove < text.size() &&
               text.at(remove) == ' ')
            ++remove;
    }
    if (remove == 0)
        return false;

    QTextCursor c = cur;
    c.setPosition(block.position());
    c.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    return true;
}

void CodeEditor::setTheme(const Theme& t) {    QPalette p = palette();
    p.setColor(QPalette::Base, t.editorBg);
    p.setColor(QPalette::Text, t.editorFg);
    p.setColor(QPalette::Highlight, t.selectionBg);
    p.setColor(QPalette::HighlightedText, t.editorFg);
    p.setColor(QPalette::ToolTipBase, t.editorBg);
    p.setColor(QPalette::ToolTipText, t.editorFg);
    setPalette(p);

    m_currentLineBg = t.currentLineBg;
    m_lnBg = t.lineNumBg;
    m_lnFg = t.lineNumFg;
    m_lnActiveFg = t.lineNumActiveFg;
    m_diagError = t.diagError;
    m_diagWarning = t.diagWarning;
    m_diagInfo = t.diagInfo;

    if (m_completer && m_completer->popup()) {
        QPalette pp = m_completer->popup()->palette();
        pp.setColor(QPalette::Base, t.editorBg);
        pp.setColor(QPalette::Text, t.editorFg);
        pp.setColor(QPalette::Highlight, t.selectionBg);
        pp.setColor(QPalette::HighlightedText, t.editorFg);
        pp.setColor(QPalette::AlternateBase, t.currentLineBg);
        m_completer->popup()->setPalette(pp);
    }

    highlightCurrentLine();
    m_lineNumberArea->update();
    viewport()->update();
}

void CodeEditor::highlightCurrentLine() { updateExtraSelections(); }

QVector<DiagnosticMark> CodeEditor::allDiagnostics() const {
    QVector<DiagnosticMark> all = m_diags;
    all += m_buildDiags;
    return all;
}

void CodeEditor::setDiagnostics(const QVector<DiagnosticMark>& diags) {
    m_diags = diags;
    rebuildDiagnosticIndex();
    updateExtraSelections();
    m_lineNumberArea->update();
    viewport()->update();
}

void CodeEditor::setBuildDiagnostics(const QVector<DiagnosticMark>& diags) {
    m_buildDiags = diags;
    rebuildDiagnosticIndex();
    updateExtraSelections();
    m_lineNumberArea->update();
    viewport()->update();
}

void CodeEditor::rebuildDiagnosticIndex() {
    m_diagLineSeverity.clear();
    for (const DiagnosticMark& d : allDiagnostics()) {
        auto it = m_diagLineSeverity.find(d.line);
        if (it == m_diagLineSeverity.end() || d.severity < it.value())
            m_diagLineSeverity.insert(d.line, d.severity);
    }
}

QColor CodeEditor::diagnosticColor(int severity) const {
    if (severity == 1)
        return m_diagError;
    if (severity == 2)
        return m_diagWarning;
    return m_diagInfo;
}

QString CodeEditor::severityLabel(int severity) const {
    return severity == 1   ? "Error"
           : severity == 2 ? "Warning"
           : severity == 3 ? "Information"
                           : "Hint";
}

void CodeEditor::updateExtraSelections() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(m_currentLineBg);
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.cursor = textCursor();
    currentLine.cursor.clearSelection();
    extraSelections.append(currentLine);

    // VS Code-style: several diagnostics on the exact same range are drawn as
    // one squiggle, colored by the most severe of them.
    QHash<QString, int> seen;
    QVector<DiagnosticMark> marks;
    for (const DiagnosticMark& d : allDiagnostics()) {
        const QString key = QString("%1:%2:%3:%4")
                                .arg(d.line)
                                .arg(d.col)
                                .arg(d.endLine)
                                .arg(d.endCol);
        auto it = seen.find(key);
        if (it == seen.end()) {
            seen.insert(key, marks.size());
            marks.append(d);
        } else if (d.severity < marks[it.value()].severity) {
            marks[it.value()].severity = d.severity;
        }
    }

    QTextDocument* doc = document();
    for (const DiagnosticMark& d : marks) {
        QTextBlock block = doc->findBlockByNumber(d.line);
        if (!block.isValid())
            continue;
        const QString text = block.text();
        const int lineLen = text.size();
        const int startCol = qBound(0, d.col, lineLen);
        int endCol = lineLen;
        if (d.endLine <= d.line && d.endCol >= 0) {
            if (d.endCol <= startCol) {
                // The compiler only reports a start position: underline the
                // rest of the token on that line.
                endCol = startCol;
                while (endCol < lineLen && !text.at(endCol).isSpace() &&
                       !QStringLiteral("(){}[];,.").contains(text.at(endCol)))
                    ++endCol;
            } else {
                endCol = qMin(d.endCol, lineLen);
            }
        }
        if (endCol <= startCol)
            endCol = qMin(lineLen, startCol + 1);
        if (endCol <= startCol)
            continue; // empty line: nothing to underline

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(doc);
        selection.cursor.setPosition(block.position() + startCol);
        selection.cursor.setPosition(block.position() + endCol,
                                     QTextCursor::KeepAnchor);
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(diagnosticColor(d.severity));
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void CodeEditor::mouseMoveEvent(QMouseEvent* e) {
    QPlainTextEdit::mouseMoveEvent(e);

    const QVector<DiagnosticMark> diags = allDiagnostics();
    if (diags.isEmpty()) {
        QToolTip::hideText();
        return;
    }

    // Hovering anywhere on the line lists every problem on that line, the way
    // VS Code shows all diagnostics for the hovered range.
    const int line = cursorForPosition(e->pos()).blockNumber();
    QVector<DiagnosticMark> hits;
    for (const DiagnosticMark& d : diags) {
        if (d.line == line || (d.endLine > d.line && line > d.line &&
                               line <= d.endLine))
            hits.append(d);
    }
    if (hits.isEmpty()) {
        QToolTip::hideText();
        return;
    }

    std::sort(hits.begin(), hits.end(), [](const DiagnosticMark& a,
                                           const DiagnosticMark& b) {
        return a.col < b.col || (a.col == b.col && a.severity < b.severity);
    });

    QString html;
    QVector<DiagnosticMark> unique;
    for (const DiagnosticMark& d : hits) {
        const bool duplicate =
            std::any_of(unique.cbegin(), unique.cend(),
                        [&d](const DiagnosticMark& u) {
                            return u.message == d.message;
                        });
        if (!duplicate)
            unique.append(d);
    }
    const int shown = qMin(unique.size(), 8);
    for (int i = 0; i < shown; ++i) {
        const DiagnosticMark& d = unique.at(i);
        html += QString("<span style='color:%1'><b>%2</b></span>: %3<br>")
                    .arg(diagnosticColor(d.severity).name(),
                         severityLabel(d.severity),
                         d.message.toHtmlEscaped());
    }
    if (unique.size() > shown)
        html += QString("… and %1 more in the Problems pane")
                    .arg(unique.size() - shown);
    if (html.endsWith("<br>"))
        html.chop(4);

    QToolTip::showText(e->globalPosition().toPoint(), html, this);
}

void CodeEditor::leaveEvent(QEvent* e) {
    QToolTip::hideText();
    QPlainTextEdit::leaveEvent(e);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_lnBg);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            if (blockNumber == textCursor().blockNumber()) {
                painter.fillRect(QRect(0, top, m_lineNumberArea->width(),
                                       fontMetrics().height()),
                                 m_lnBg.darker(115));
                painter.setPen(m_lnActiveFg);
            } else {
                painter.setPen(m_lnFg);
            }
            painter.drawText(0, top, m_lineNumberArea->width(),
                             fontMetrics().height(), Qt::AlignRight, number);

            auto diag = m_diagLineSeverity.constFind(blockNumber);
            if (diag != m_diagLineSeverity.constEnd()) {
                painter.fillRect(
                    QRect(0, top, 3, qMin(fontMetrics().height(), bottom - top)),
                    diagnosticColor(diag.value()));
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::scheduleCompletion() {
    if (m_inserting)
        return;
    m_completionTimer->start();
}

void CodeEditor::keyPressEvent(QKeyEvent* e) {
    // While the popup is visible, let it handle navigation / acceptance keys
    // (they are delivered to the active popup by Qt's event system).
    if (m_completer && m_completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool plain =
        !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));

    // --- indentation --------------------------------------------------
    if (plain && (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab)) {
        indentSelection(e->key() == Qt::Key_Backtab ||
                        (mods & Qt::ShiftModifier));
        return;
    }
    if (plain && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        insertNewlineWithIndent();
        return;
    }
    if (plain && e->key() == Qt::Key_Backspace && deleteBracketPair())
        return;

    // --- auto pairs ---------------------------------------------------
    if (plain) {
        const QString typed = e->text();
        if (typed.size() == 1) {
            const QChar ch = typed.at(0);
            const bool quote = (ch == '"' || ch == '\'' || ch == '`');
            const bool opening = (ch == '(' || ch == '[' || ch == '{');
            const bool closing = (ch == ')' || ch == ']' || ch == '}');

            if (quote) {
                if (skipClosing(ch))
                    return;
                if (insertPair(ch))
                    return;
            } else if (opening) {
                if (insertPair(ch))
                    return;
            } else if (closing) {
                if (skipClosing(ch))
                    return;
                // typing } ) ] on an indented empty line: outdent it first
                dedentIfOnlyWhitespace();
            }
        }
    }

    QPlainTextEdit::keyPressEvent(e);

    // Ctrl+Space -> manual completion
    if (e->modifiers() & Qt::ControlModifier && e->key() == Qt::Key_Space) {
        scheduleCompletion();
        return;
    }

    QString t = e->text();
    bool trigger = false;
    if (!t.isEmpty()) {
        QChar c = t.at(0);
        trigger = c.isLetterOrNumber() || c == '_' || c == '.' || c == ':' ||
                  c == '>' || c == '~' || c == '(';
    }
    if (e->key() == Qt::Key_Backspace)
        trigger = true;

    if (trigger)
        scheduleCompletion();
    else if (m_completer && m_completer->popup()->isVisible() && !t.isEmpty())
        m_completer->popup()->hide();
}
