#include "codeeditor.h"

#include <QPainter>
#include <QTextBlock>
#include <QTextEdit>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QKeyEvent>

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

void CodeEditor::setTheme(const Theme& t) {
    QPalette p = palette();
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

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(m_currentLineBg);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    extraSelections.append(selection);

    setExtraSelections(extraSelections);
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
