#pragma once

#include <QFont>
#include <QHash>
#include <QMainWindow>
#include <QProcess>
#include <QStringList>
#include <QVector>

#include "clangdclient.h"
#include "languages.h"
#include "themeh.h"

class CodeEditor;
class CppHighlighter;
class QCompleter;
class QStringListModel;
class QPlainTextEdit;
class QTableWidget;
class QLineEdit;
class QLabel;
class QTabWidget;
class QCloseEvent;
class QStackedWidget;
class QMenu;
class WelcomePage;

class Ide : public QMainWindow {
    Q_OBJECT
public:
    explicit Ide(QWidget* parent = nullptr);
    void openFileFromPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void closeTab(int index);
    void onTabChanged(int index);
    void compileOnly();
    void compileAndRun();
    void stopProgram();
    void toggleComment();
    void onBuildFinished(int exitCode, QProcess::ExitStatus status);
    void onRunFinished(int exitCode, QProcess::ExitStatus status);
    void sendProgramInput();
    void onCompletionRequested();
    void onCompletionReady(bool ok, const QVector<ClangdItem>& items);
    void insertCompletion(const QModelIndex& index);
    void onDiagnostics(const QString& path,
                       const QVector<ClangdDiagnostic>& diags);
    void onCellDoubleClicked(int row, int column);
    void updateStatusCursor();
    void showHome();
    void showEditorPage();

private:
    struct Doc; // one open file / tab

    void setupActions();
    void setupUi();
    Doc* createDoc(const QString& path, const QString& content);
    Doc* activeDoc() const;
    Doc* docAt(int index) const;
    void setActiveDoc(Doc* doc);
    void removeDoc(Doc* doc);
    void updateTabTitle(Doc* doc);
    void updateWindowTitle();
    bool saveDoc(Doc* doc);
    bool saveDocAs(Doc* doc);
    bool maybeSaveDoc(Doc* doc);
    bool maybeSaveAll();
    void startBuild(bool runAfter);
    void runProgram();
    void appendConsole(const QString& text);
    void parseBuildErrors(const QString& output);
    void addProblemRow(const QString& severity, const QString& message,
                       const QString& file, int line, int col);
    void jumpTo(const QString& file, int line, int col);
    void syncDocument();
    void applyTheme(const Theme& theme);
    bool askToolchain(LanguageSpec& spec);
    void showLanguageSettings();
    void loadRecent();
    void saveRecent();
    void addRecent(const QString& path);
    void refreshRecent();
    void rebuildRecentMenu();

    // open documents / tabs
    QTabWidget* m_docTabs = nullptr;
    QHash<CodeEditor*, Doc*> m_docs;
    QVector<Doc*> m_docList;
    Theme m_theme;
    QFont m_editorFont;
    int m_untitledCounter = 0;

    // active document (shortcuts into the active Doc)
    CodeEditor* m_editor = nullptr;
    CppHighlighter* m_highlighter = nullptr;
    QString m_currentFile;

    // completion
    QCompleter* m_completer = nullptr;
    QStringListModel* m_completerModel = nullptr;
    QVector<ClangdItem> m_items;
    ClangdClient* m_clangd = nullptr;
    int m_prefixStart = 0; // cursor position where current word starts

    // build / run
    QProcess* m_buildProc = nullptr;
    QProcess* m_runProc = nullptr;
    bool m_runAfterBuild = false;
    QString m_binaryPath;
    QString m_buildFile; // file the current build/run belongs to
    QString m_buildOutput;
    LanguageRegistry m_langs;
    LanguageSpec m_activeSpec;

    // widgets
    QPlainTextEdit* m_console = nullptr;
    QTableWidget* m_problems = nullptr;
    QLineEdit* m_inputLine = nullptr;
    QLabel* m_posLabel = nullptr;
    QLabel* m_intelLabel = nullptr;

    // home page / recent files
    QStackedWidget* m_stack = nullptr;
    WelcomePage* m_welcome = nullptr;
    QMenu* m_recentMenu = nullptr;
    QStringList m_recent;
};
