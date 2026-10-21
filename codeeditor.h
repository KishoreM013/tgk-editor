#pragma once

#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QColor>
#include <QHash>
#include <QVector>

#include "themeh.h"

// One underlined problem in the editor. Positions are 0-based; an endCol of
// -1 underlines the rest of the line, and endCol <= col underlines the word.
struct DiagnosticMark {
    int line = 0;
    int col = 0;
    int endLine = -1;
    int endCol = -1;
    int severity = 1; // 1 error, 2 warning, 3+ note
    bool unnecessary = false; // LSP hint (unused include etc.)
    QString message;
};

// Source editor: line numbers, current-line highlight and completion popup
// plumbing (the actual completion data comes from the Ide via clangd).
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setCompleter(QCompleter* c);
    QCompleter* completer() const { return m_completer; }
    void setTheme(const Theme& theme);
    void setTabWidth(int width, bool useTabs);

    // Language-server diagnostics (clangd) and compiler warnings/errors are
    // kept separately so a build can clear compiler marks without touching
    // the IntelliSense ones.
    void setDiagnostics(const QVector<DiagnosticMark>& diags);
    void setBuildDiagnostics(const QVector<DiagnosticMark>& diags);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth() const;

signals:
    void completionRequested(); // debounced; Ide queries clangd on this

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);
    void scheduleCompletion();

private:
    QString indentUnit() const;
    void indentSelection(bool outdent);
    void insertNewlineWithIndent();
    bool insertPair(QChar open);
    bool skipClosing(QChar close);
    bool deleteBracketPair();
    bool dedentIfOnlyWhitespace();
    void hideCompletionPopup();

    QVector<DiagnosticMark> allDiagnostics() const;
    void rebuildDiagnosticIndex();
    void updateExtraSelections();
    QColor diagnosticColor(int severity) const;
    QString severityLabel(int severity) const;

    QWidget* m_lineNumberArea;
    QCompleter* m_completer = nullptr;
    QTimer* m_completionTimer;
    bool m_inserting = false;
    int m_tabWidth = 4;
    bool m_useTabs = false;

    QVector<DiagnosticMark> m_diags;      // from clangd
    QVector<DiagnosticMark> m_buildDiags; // from the compiler
    QHash<int, int> m_diagLineSeverity;   // line -> worst severity

    // theme colors
    QColor m_currentLineBg = QColor("#EAF1FB");
    QColor m_lnBg = QColor("#F0F0F0");
    QColor m_lnFg = QColor("#8A8A8A");
    QColor m_lnActiveFg = QColor("#1E1E1E");
    QColor m_diagError = QColor("#D13438");
    QColor m_diagWarning = QColor("#B7791F");
    QColor m_diagInfo = QColor("#2B6CB0");
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
