#pragma once

#include <QMainWindow>
#include <QProcess>
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

private:
    void setupActions();
    void setupUi();
    void startBuild(bool runAfter);
    void runProgram();
    void appendConsole(const QString& text);
    void parseBuildErrors(const QString& output);
    void addProblemRow(const QString& severity, const QString& message,
                       const QString& file, int line, int col);
    void jumpTo(const QString& file, int line, int col);
    void syncDocument();
    bool maybeSave();
    QString defaultTitle() const;
    void applyTheme(const Theme& theme);
    void updateLanguageForFile();
    bool askToolchain(LanguageSpec& spec);
    void showLanguageSettings();
    void showHome();
    void closeFile();
    void loadRecent();
    void saveRecent();
    void addRecent(const QString& path);
    void refreshRecent();
    void rebuildRecentMenu();

    // editor
    CodeEditor* m_editor = nullptr;
    CppHighlighter* m_highlighter = nullptr;

    // completion
    QCompleter* m_completer = nullptr;
    QStringListModel* m_completerModel = nullptr;
    QVector<ClangdItem> m_items;
    ClangdClient* m_clangd = nullptr;
    QString m_openPath;    // file currently open in clangd
    int m_prefixStart = 0; // cursor position where current word starts

    // build / run
    QProcess* m_buildProc = nullptr;
    QProcess* m_runProc = nullptr;
    bool m_runAfterBuild = false;
    QString m_binaryPath;
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

    // document
    QString m_currentFile;
};
