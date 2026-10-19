#pragma once

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QColor>

#include "themeh.h"

// Source editor: line numbers, current-line highlight and completion popup
// plumbing (the actual completion data comes from the Ide via clangd).
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setCompleter(QCompleter* c);
    QCompleter* completer() const { return m_completer; }
    void setTheme(const Theme& theme);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth() const;

signals:
    void completionRequested(); // debounced; Ide queries clangd on this

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);
    void scheduleCompletion();

private:
    QWidget* m_lineNumberArea;
    QCompleter* m_completer = nullptr;
    QTimer* m_completionTimer;
    bool m_inserting = false;

    // theme colors
    QColor m_currentLineBg = QColor("#EAF1FB");
    QColor m_lnBg = QColor("#F0F0F0");
    QColor m_lnFg = QColor("#8A8A8A");
    QColor m_lnActiveFg = QColor("#1E1E1E");
};

// Thin widget drawn to the left of the editor.
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor)
        : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override {
        return {m_editor->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor* m_editor;
};
