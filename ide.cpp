#include "ide.h"
#include "codeeditor.h"
#include "highlighter.h"
#include "projectpanel.h"
#include "welcomepage.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QAbstractItemView>
#include <QStatusBar>
#include <QStringListModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

// One open file / tab. The active editor shortcuts (m_editor, m_highlighter,
// m_currentFile) always point at the currently selected Doc.
struct Ide::Doc {
    CodeEditor* editor = nullptr;
    CppHighlighter* highlighter = nullptr;
    QString path;     // empty = never saved
    QString untitled; // display name while unsaved
    bool clangdOpen = false;
    bool pinned = false;

    QString displayName() const {
        return path.isEmpty() ? untitled : QFileInfo(path).fileName();
    }
};

Ide::Ide(QWidget* parent) : QMainWindow(parent) {
    m_langs.load(); // built-ins + ~/.config/tgk/tgkeditor.json

    m_tabWidth = qBound(1, QSettings().value("tabWidth", 4).toInt(), 16);
    m_useTabs = QSettings().value("useTabs", false).toBool();
    m_autoSave = QSettings().value("autoSave", false).toBool();

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(1200);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &Ide::autoSaveNow);

    m_editorFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_editorFont.setPointSize(11);
    m_origPalette = qApp->palette();

    m_completer = new QCompleter(this);
    m_completerModel = new QStringListModel(this);
    m_completer->setModel(m_completerModel);
    connect(m_completer, QOverload<const QModelIndex&>::of(&QCompleter::activated),
            this, &Ide::insertCompletion);

    m_clangd = new ClangdClient(this);
    connect(m_clangd, &ClangdClient::diagnostics, this, &Ide::onDiagnostics);
    connect(m_clangd, &ClangdClient::serverDied, this, [this] {
        m_intelLabel->setText("IntelliSense: offline");
        for (Doc* d : m_docList)
            d->clangdOpen = false;
    });

    m_buildProc = new QProcess(this);
    m_buildProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_buildProc, &QProcess::readyReadStandardOutput, this, [this] {
        QString s = QString::fromUtf8(m_buildProc->readAllStandardOutput());
        m_buildOutput += s;
        appendConsole(s);
    });
    connect(m_buildProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Ide::onBuildFinished);

    m_runProc = new QProcess(this);
    m_runProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_runProc, &QProcess::readyReadStandardOutput, this, [this] {
        appendConsole(QString::fromUtf8(m_runProc->readAllStandardOutput()));
    });
    connect(m_runProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Ide::onRunFinished);

    m_terminalProc = new QProcess(this);
    m_terminalProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_terminalProc, &QProcess::readyReadStandardOutput, this, [this] {
        m_terminalOutput->moveCursor(QTextCursor::End);
        m_terminalOutput->insertPlainText(
            QString::fromUtf8(m_terminalProc->readAllStandardOutput()));
        m_terminalOutput->moveCursor(QTextCursor::End);
    });

    setupUi();
    setupActions();

    applyTheme(findTheme(QSettings().value("theme", "zed-dark").toString()));

    loadRecent();
    refreshRecent();

    QString projectRoot =
        QSettings().value("projectRoot", QDir::homePath()).toString();
    if (!QFileInfo(projectRoot).isDir())
        projectRoot = QDir::homePath();
    m_project->setRoot(projectRoot);

    resize(1200, 800);
    setWindowTitle("Home");
    statusBar()->showMessage("Ready — F9 compiles & runs, Ctrl+Space completes");
}

void Ide::setAutoSave(bool enabled) {
    m_autoSave = enabled;
    QSettings s;
    s.setValue("autoSave", m_autoSave);
    s.sync();
    if (m_autoSaveAction && m_autoSaveAction->isChecked() != enabled) {
        QSignalBlocker blocker(m_autoSaveAction);
        m_autoSaveAction->setChecked(enabled);
    }
    if (enabled) {
        m_autoSaveTimer->start(); // catch up on anything already modified
        statusBar()->showMessage("Auto-save on", 2000);
    } else {
        m_autoSaveTimer->stop();
        statusBar()->showMessage("Auto-save off", 2000);
    }
}

void Ide::autoSaveNow() {
    if (!m_autoSave)
        return;
    for (Doc* doc : m_docList) {
        if (doc->path.isEmpty() || !doc->editor->document()->isModified())
            continue; // unsaved untitled files need a name first
        saveDoc(doc);
    }
}

void Ide::setIndent(int width, bool useTabs) {    m_tabWidth = qBound(1, width, 16);
    m_useTabs = useTabs;
    for (Doc* doc : m_docList)
        doc->editor->setTabWidth(m_tabWidth, m_useTabs);

    QSettings s;
    s.setValue("tabWidth", m_tabWidth);
    s.setValue("useTabs", m_useTabs);
    s.sync();

    statusBar()->showMessage(
        m_useTabs ? QString("Indentation: tab characters")
                  : QString("Indentation: %1 spaces").arg(m_tabWidth),
        2500);
}

static QString zedChromeStyle(const Theme& t) {
    const bool dark = t.editorBg.lightness() < 128;
    const QString hover = dark ? "#363C46" : "#DFDFE0";
    const QString selected = dark ? "#454A56" : "#CACACA";
    const QString thumb = dark ? "#4E5561" : "#C3C3C8";
    QString css = QStringLiteral(R"CSS(
QMainWindow, QDialog { background: %WIN%; }
QMenuBar { background: %PANEL%; color: %MUTED%; padding: 1px 6px; border-bottom: 1px solid %BORDER%; }
QMenuBar::item { background: transparent; padding: 3px 8px; border-radius: 4px; }
QMenuBar::item:selected { background: %HOVER%; }
QMenu { background: %PANEL%; color: %TEXT%; border: 1px solid %BORDER%; padding: 4px; }
QMenu::item { padding: 5px 26px 5px 18px; border-radius: 4px; }
QMenu::item:selected { background: %SELECTED%; color: %TEXT%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }
QToolButton { background: transparent; color: %TEXT%; border: none; border-radius: 4px; padding: 4px 10px; }
QToolButton:hover { background: %HOVER%; }
QToolButton:pressed { background: %SELECTED%; }
QStatusBar { background: %WIN%; color: %MUTED%; border-top: 1px solid %BORDER%; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %MUTED%; padding: 0 6px; }
QTabWidget::pane { border: none; }
QTabBar { background: %PANEL%; }
QTabBar::tab { background: %PANEL%; color: %MUTED%; border: none; padding: 7px 12px; margin-right: 1px; }
QTabBar::tab:selected { background: %EDITOR%; color: %TEXT%; }
QTabBar::tab:hover:!selected { background: %HOVER%; }
QDockWidget { color: %TEXT%; border: none; }
QDockWidget::title { background: %PANEL%; padding: 7px 10px; border-bottom: 1px solid %BORDER%; }
QDockWidget::close-button, QDockWidget::float-button { background: transparent; border: none; }
#dockTitleBar { background: %PANEL%; border-bottom: 1px solid %BORDER%; }
#dockTitleBar QLabel { color: %TEXT%; }
QSplitter::handle { background: %BORDER%; }
QHeaderView::section { background: %PANEL%; color: %MUTED%; border: none; border-bottom: 1px solid %BORDER%; padding: 4px 6px; }
QTableWidget { border: none; gridline-color: %BORDER%; }
QLineEdit, QPlainTextEdit { border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %THUMB%; min-height: 24px; border-radius: 5px; }
QScrollBar::handle:vertical:hover { background: %MUTED%; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: %THUMB%; min-width: 24px; border-radius: 5px; }
QScrollBar::handle:horizontal:hover { background: %MUTED%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)CSS");
    css.replace("%WIN%", t.windowBg.name());
    css.replace("%PANEL%", t.panelBg.name());
    css.replace("%BORDER%", t.border.name());
    css.replace("%TEXT%", t.editorFg.name());
    css.replace("%MUTED%", t.mutedFg.name());
    css.replace("%EDITOR%", t.editorBg.name());
    css.replace("%HOVER%", hover);
    css.replace("%SELECTED%", selected);
    css.replace("%THUMB%", thumb);
    return css;
}

void Ide::applyTheme(const Theme& t) {
    m_theme = t;

    // Zed themes also color the window chrome (menus, tabs, toolbar, status
    // bar). Other themes keep the system palette.
    if (t.windowBg.isValid()) {
        const bool dark = t.editorBg.lightness() < 128;
        QPalette p = m_origPalette;
        p.setColor(QPalette::Window, t.windowBg);
        p.setColor(QPalette::WindowText, t.editorFg);
        p.setColor(QPalette::Base, t.editorBg);
        p.setColor(QPalette::AlternateBase, t.panelBg);
        p.setColor(QPalette::Text, t.editorFg);
        p.setColor(QPalette::Button, t.panelBg);
        p.setColor(QPalette::ButtonText, t.editorFg);
        p.setColor(QPalette::Highlight,
                   QColor(dark ? "#454A56" : "#CACACA"));
        p.setColor(QPalette::HighlightedText, t.editorFg);
        p.setColor(QPalette::ToolTipBase, t.panelBg);
        p.setColor(QPalette::ToolTipText, t.editorFg);
        p.setColor(QPalette::PlaceholderText, t.mutedFg);
        p.setColor(QPalette::Link, t.accent);
        p.setColor(QPalette::Disabled, QPalette::Text, t.mutedFg);
        p.setColor(QPalette::Disabled, QPalette::WindowText, t.mutedFg);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, t.mutedFg);
        qApp->setPalette(p);
        qApp->setStyleSheet(zedChromeStyle(t));
    } else {
        qApp->setStyleSheet(QString());
        qApp->setPalette(m_origPalette);
    }

    for (Doc* d : m_docList) {
        d->editor->setTheme(t);
        d->highlighter->setTheme(t);
    }

    QPalette cp = m_console->palette();
    cp.setColor(QPalette::Base, t.consoleBg);
    cp.setColor(QPalette::Text, t.consoleFg);
    m_console->setPalette(cp);

    QPalette ip = m_inputLine->palette();
    ip.setColor(QPalette::Base, t.editorBg);
    ip.setColor(QPalette::Text, t.editorFg);
    m_inputLine->setPalette(ip);

    QPalette tp = m_problems->palette();
    tp.setColor(QPalette::Base, t.consoleBg);
    tp.setColor(QPalette::Text, t.consoleFg);
    tp.setColor(QPalette::AlternateBase, t.currentLineBg);
    m_problems->setPalette(tp);

    if (m_welcome)
        m_welcome->applyTheme(t);
    if (m_project)
        m_project->applyTheme(t);
}

void Ide::setupUi() {
    auto* splitter = new QSplitter(Qt::Vertical, this);

    auto* bottom = new QWidget(splitter);
    auto* bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);

    auto* tabs = new QTabWidget(bottom);

    auto* outputTab = new QWidget(tabs);
    auto* outLayout = new QVBoxLayout(outputTab);
    outLayout->setContentsMargins(0, 0, 0, 0);
    m_console = new QPlainTextEdit(outputTab);
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(5000);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    m_console->setFont(mono);
    outLayout->addWidget(m_console);
    m_inputLine = new QLineEdit(outputTab);
    m_inputLine->setPlaceholderText(
        "Program input here (Enter sends a line to the running program)…");
    m_inputLine->setEnabled(false);
    connect(m_inputLine, &QLineEdit::returnPressed, this, &Ide::sendProgramInput);
    outLayout->addWidget(m_inputLine);
    tabs->addTab(outputTab, "Output");

    m_problems = new QTableWidget(tabs);
    m_problems->setColumnCount(5);
    m_problems->setHorizontalHeaderLabels(
        {"Severity", "Message", "File", "Line", "Col"});
    m_problems->horizontalHeader()->setStretchLastSection(false);
    m_problems->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_problems->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_problems->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_problems->verticalHeader()->setVisible(false);
    connect(m_problems, &QTableWidget::cellDoubleClicked, this,
            &Ide::onCellDoubleClicked);
    tabs->addTab(m_problems, "Problems");

    auto* terminalTab = new QWidget(tabs);
    auto* terminalLayout = new QVBoxLayout(terminalTab);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    m_terminalOutput = new QPlainTextEdit(terminalTab);
    m_terminalOutput->setReadOnly(true);
    m_terminalOutput->setFont(mono);
    terminalLayout->addWidget(m_terminalOutput);
    m_terminalInput = new QLineEdit(terminalTab);
    m_terminalInput->setPlaceholderText("Terminal command…");
    connect(m_terminalInput, &QLineEdit::returnPressed, this, [this] {
        const QString command = m_terminalInput->text();
        if (command.isEmpty())
            return;
        if (m_terminalProc->state() == QProcess::NotRunning)
            m_terminalProc->start("/bin/bash", {"-i"});
        m_terminalProc->write(command.toUtf8() + "\n");
        m_terminalInput->clear();
    });
    terminalLayout->addWidget(m_terminalInput);
    tabs->addTab(terminalTab, "Terminal");

    bottomLayout->addWidget(tabs);

    m_docTabs = new QTabWidget(splitter);
    m_docTabs->setTabsClosable(true);
    m_docTabs->setMovable(true);
    m_docTabs->setDocumentMode(true);
    m_docTabs->setElideMode(Qt::ElideRight);
    connect(m_docTabs, &QTabWidget::tabCloseRequested, this, &Ide::closeTab);
    connect(m_docTabs, &QTabWidget::currentChanged, this, &Ide::onTabChanged);
    m_docTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_docTabs, &QTabWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const int index = m_docTabs->tabBar()->tabAt(pos);
                if (index < 0)
                    return;
                auto* doc = docAt(index);
                QMenu menu(this);
                menu.addAction(doc && doc->pinned ? "Unpin Tab" : "Pin Tab",
                               this, [this, index] { toggleTabPinned(index); });
                menu.addSeparator();
                menu.addAction("Close Tab", this,
                               [this, index] { closeTab(index); });
                menu.exec(m_docTabs->tabBar()->mapToGlobal(pos));
            });

    splitter->addWidget(m_docTabs);
    splitter->addWidget(bottom);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    // home page (index 0) / editor (index 1)
    m_welcome = new WelcomePage(this);
    connect(m_welcome, &WelcomePage::newFileRequested, this, &Ide::newFile);
    connect(m_welcome, &WelcomePage::openFileRequested, this, &Ide::openFile);
    connect(m_welcome, &WelcomePage::openProjectRequested, this,
            [this] { m_project->openFolderDialog(); });
    connect(m_welcome, &WelcomePage::fileActivated, this,
            &Ide::openFileFromPath);
    connect(m_welcome, &WelcomePage::backToEditorRequested, this,
            &Ide::showEditorPage);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_welcome);
    m_stack->addWidget(splitter);
    setCentralWidget(m_stack);

    // Project sidebar: folder tree with per-file pins.
    m_projectDock = new QDockWidget("Explorer", this);
    m_projectDock->setObjectName("projectDock");
    m_projectDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                   Qt::RightDockWidgetArea);
    m_project = new ProjectPanel(m_projectDock);
    m_projectDock->setWidget(m_project);

    // Explorer title bar: pinned-only box right next to the close button.
    auto* dockTitle = new QWidget(m_projectDock);
    dockTitle->setObjectName("dockTitleBar");
    dockTitle->setAutoFillBackground(true);
    auto* dockTitleLayout = new QHBoxLayout(dockTitle);
    dockTitleLayout->setContentsMargins(8, 3, 4, 3);
    dockTitleLayout->setSpacing(6);
    auto* dockTitleLabel = new QLabel("Explorer", dockTitle);
    QFont dockTitleFont = dockTitleLabel->font();
    dockTitleFont.setBold(true);
    dockTitleLabel->setFont(dockTitleFont);
    dockTitleLayout->addWidget(dockTitleLabel);
    dockTitleLayout->addStretch(1);
    if (QCheckBox* pinnedOnly = m_project->pinnedOnlyToggle()) {
        pinnedOnly->setParent(dockTitle);
        dockTitleLayout->addWidget(pinnedOnly);
        pinnedOnly->show();
    }
    auto* dockClose = new QToolButton(dockTitle);
    dockClose->setText(QString::fromUtf8("×"));
    dockClose->setToolTip("Hide the Explorer");
    dockClose->setAutoRaise(true);
    dockClose->setFixedSize(20, 20);
    connect(dockClose, &QToolButton::clicked, m_projectDock,
            &QDockWidget::close);
    dockTitleLayout->addWidget(dockClose);
    m_projectDock->setTitleBarWidget(dockTitle);

    m_projectDock->setMinimumWidth(180);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);
    resizeDocks({m_projectDock}, {250}, Qt::Horizontal);

    connect(m_project, &ProjectPanel::fileActivated, this,
            &Ide::openFileFromPath);
    connect(m_project, &ProjectPanel::rootChanged, this,
            [this](const QString& dir) {
                QSettings s;
                s.setValue("projectRoot", dir);
                s.sync();
                statusBar()->showMessage("Folder: " + dir, 2500);
            });

    m_posLabel = new QLabel("Ln 1, Col 1", this);
    m_intelLabel = new QLabel("IntelliSense: off", this);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_intelLabel);
}

void Ide::setupActions() {
    // File menu
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New", this, &Ide::newFile, QKeySequence::New);
    fileMenu->addAction("&Open…", this, &Ide::openFile, QKeySequence::Open);
    fileMenu->addAction("Open &Project…", this,
                        [this] { m_project->openFolderDialog(); },
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    fileMenu->addAction("Open &Terminal", this, &Ide::showTerminal,
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T));
    m_recentMenu = fileMenu->addMenu("Recent &Files");
    fileMenu->addSeparator();
    fileMenu->addAction("&Save", this, &Ide::saveFile, QKeySequence::Save);
    fileMenu->addAction("Save &As…", this, &Ide::saveFileAs,
                        QKeySequence::SaveAs);
    fileMenu->addSeparator();
    m_autoSaveAction = fileMenu->addAction("Auto-&save");
    m_autoSaveAction->setObjectName("autoSaveAction");
    m_autoSaveAction->setCheckable(true);
    m_autoSaveAction->setChecked(m_autoSave);
    m_autoSaveAction->setToolTip(
        "Automatically save files 1.2 s after you stop typing");
    connect(m_autoSaveAction, &QAction::toggled, this, &Ide::setAutoSave);
    fileMenu->addSeparator();
    fileMenu->addAction("&Close Tab", this,
                        [this] { closeTab(m_docTabs->currentIndex()); },
                        QKeySequence(Qt::CTRL | Qt::Key_W));
    fileMenu->addAction("&Home / Welcome Page", this, &Ide::showHome,
                        QKeySequence(Qt::CTRL | Qt::Key_H));
    fileMenu->addAction("&Language Settings…", this,
                        &Ide::showLanguageSettings);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", qApp, &QApplication::quit,
                        QKeySequence::Quit);

    // Edit menu
    QMenu* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("&Toggle Comment", this, &Ide::toggleComment,
                        QKeySequence(Qt::CTRL | Qt::Key_Backslash));

    QMenu* indentMenu = editMenu->addMenu("&Indentation");
    QActionGroup* indentGroup = new QActionGroup(this);
    for (int w : {2, 3, 4, 8}) {
        QAction* a = indentMenu->addAction(QString("%1 spaces").arg(w));
        a->setCheckable(true);
        a->setActionGroup(indentGroup);
        a->setChecked(!m_useTabs && w == m_tabWidth);
        connect(a, &QAction::triggered, this,
                [this, w] { setIndent(w, false); });
    }
    indentMenu->addSeparator();
    QAction* tabChars = indentMenu->addAction("Use &tab characters");
    tabChars->setCheckable(true);
    tabChars->setChecked(m_useTabs);
    connect(tabChars, &QAction::toggled, this,
            [this](bool on) { setIndent(m_tabWidth, on); });

    // Build menu
    QMenu* buildMenu = menuBar()->addMenu("&Build");
    buildMenu->addAction("&Compile (F8)", this, &Ide::compileOnly,
                         QKeySequence(Qt::Key_F8));
    buildMenu->addAction("&Compile & Run (F9)", this, &Ide::compileAndRun,
                         QKeySequence(Qt::Key_F9));
    buildMenu->addAction("&Stop program", this, &Ide::stopProgram,
                         QKeySequence(Qt::Key_F10));

    // View menu — sidebar and themes
    QMenu* viewMenu = menuBar()->addMenu("&View");
    QAction* projectToggle = m_projectDock->toggleViewAction();
    projectToggle->setText("Project &Sidebar");
    projectToggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    projectToggle->setToolTip("Show/hide the project sidebar (Ctrl+B)");
    viewMenu->addAction(projectToggle);
    viewMenu->addSeparator();
    QActionGroup* themeGroup = new QActionGroup(this);
    QString currentTheme =
        QSettings().value("theme", "zed-dark").toString();
    for (const Theme& t : builtinThemes()) {
        QAction* a = viewMenu->addAction(t.name);
        a->setCheckable(true);
        a->setActionGroup(themeGroup);
        a->setChecked(t.id == currentTheme);
        connect(a, &QAction::triggered, this, [this, t] {
            applyTheme(t);
            QSettings s;
            s.setValue("theme", t.id);
            s.sync();
            statusBar()->showMessage("Theme: " + t.name, 2000);
        });
    }

    // Help menu
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, [this] {
        QMessageBox::about(
            this, "tgk editor",
            "<b>tgk editor</b> — a tiny multi-language IDE for Linux "
            "(C, C++, Java, Python & more).<br><br>"
            "F9 = compile & run, Ctrl+Space = completion (C/C++), "
            "Ctrl+\\ = toggle comment.<br>"
            "IntelliSense powered by clangd.");
    });

    // Compile & run buttons sit at the right end of the menu bar, so no
    // separate toolbar row is needed.
    auto* runBar = new QWidget(this);
    auto* runLayout = new QHBoxLayout(runBar);
    runLayout->setContentsMargins(0, 0, 8, 0);
    runLayout->setSpacing(2);
    auto addRunButton = [&](const QString& text, const QString& tip,
                            void (Ide::*slot)()) {
        auto* b = new QToolButton(runBar);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(b, &QToolButton::clicked, this, slot);
        runLayout->addWidget(b);
    };
    addRunButton("Build", "Compile only (F8)", &Ide::compileOnly);
    addRunButton("Run", "Compile & run (F9)", &Ide::compileAndRun);
    addRunButton("Stop", "Stop the running program (F10)", &Ide::stopProgram);
    menuBar()->setCornerWidget(runBar, Qt::TopRightCorner);

    // Tab switching: Ctrl+Tab / Ctrl+Shift+Tab
    auto* nextTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    connect(nextTab, &QShortcut::activated, this, [this] {
        const int n = m_docTabs->count();
        if (n < 2)
            return;
        m_stack->setCurrentIndex(1);
        m_docTabs->setCurrentIndex((m_docTabs->currentIndex() + 1) % n);
    });
    auto* prevTab =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    connect(prevTab, &QShortcut::activated, this, [this] {
        const int n = m_docTabs->count();
        if (n < 2)
            return;
        m_stack->setCurrentIndex(1);
        m_docTabs->setCurrentIndex((m_docTabs->currentIndex() - 1 + n) % n);
    });
}

namespace {
static int editDistance(const QString& a, const QString& b) {
    QVector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int substitute =
                prev[j - 1] + (a.at(i - 1) == b.at(j - 1) ? 0 : 1);
            cur[j] = qMin(qMin(prev[j] + 1, cur[j - 1] + 1), substitute);
        }
        prev.swap(cur);
    }
    return prev.at(b.size());
}

// clangd only reports "invalid preprocessing directive". For a near miss such
// as "#inlcude", append the correction the compiler would suggest.
static QString augmentDiagnostic(const QString& message,
                                 const QString& lineText) {
    if (!message.contains(QLatin1String("invalid preprocessing directive"),
                          Qt::CaseInsensitive))
        return message;
    const QString text = lineText.trimmed();
    if (!text.startsWith('#'))
        return message;
    int end = 1;
    while (end < text.size() &&
           (text.at(end).isLetterOrNumber() || text.at(end) == '_'))
        ++end;
    const QString word = text.mid(1, end - 1);
    if (word.isEmpty())
        return message;

    static const QStringList known = {
        "include", "include_next", "define", "undef", "if", "ifdef",
        "ifndef", "elif", "else", "endif", "pragma", "error", "warning",
        "line"};
    QString best;
    int bestDistance = 3;
    for (const QString& candidate : known) {
        const int distance = editDistance(word.toLower(), candidate);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    if (!best.isEmpty() && best != word.toLower() && bestDistance <= 2)
        return message +
               QStringLiteral(" (did you mean #%1?)").arg(best);
    return message;
}

// Human-readable form for the console, e.g. g++ "/a b/x.cpp" -o "/a b/x.bin".
QString displayCommand(const QStringList& parts) {
    QStringList out;
    out.reserve(parts.size());
    for (const QString& p : parts) {
        if (p.isEmpty() || p.contains(' ') || p.contains('\t') ||
            p.contains('"')) {
            QString quoted = p;
            quoted.replace('"', "\\\"");
            out << '"' + quoted + '"';
        } else {
            out << p;
        }
    }
    return out.join(' ');
}
} // namespace

static CppHighlighter::Lang languageForPath(const LanguageRegistry& langs,
                                            const QString& path,
                                            QString* displayName = nullptr) {
    CppHighlighter::Lang lang = CppHighlighter::Lang::Cpp;
    if (displayName)
        displayName->clear();
    if (path.isEmpty()) {
        if (displayName)
            *displayName = "C++ (default for new files)";
        return lang;
    }
    const LanguageSpec* spec = langs.forFile(path);
    if (!spec) {
        if (displayName)
            *displayName = "plain text";
        return lang;
    }
    if (displayName)
        *displayName = spec->displayName;
    if (spec->id == "java")
        lang = CppHighlighter::Lang::Java;
    else if (spec->id == "python")
        lang = CppHighlighter::Lang::Python;
    return lang;
}

Ide::Doc* Ide::createDoc(const QString& path, const QString& content) {
    auto* doc = new Doc;
    doc->path = path;
    if (path.isEmpty()) {
        ++m_untitledCounter;
        doc->untitled = m_untitledCounter == 1
                            ? QString("untitled.cpp")
                            : QString("untitled%1.cpp").arg(m_untitledCounter);
    }

    doc->editor = new CodeEditor(m_docTabs);
    doc->highlighter = new CppHighlighter(doc->editor->document());
    doc->editor->setFont(m_editorFont);
    doc->editor->setTabWidth(m_tabWidth, m_useTabs);
    doc->editor->setCompleter(m_completer);
    doc->editor->setPlainText(content);
    doc->editor->document()->setModified(false);

    doc->highlighter->setLanguage(languageForPath(m_langs, path));
    doc->highlighter->setTheme(m_theme);
    doc->editor->setTheme(m_theme);

    const int index = m_docTabs->addTab(doc->editor, doc->displayName());
    m_docs.insert(doc->editor, doc);
    m_docList.append(doc);

    connect(doc->editor, &CodeEditor::completionRequested, this,
            &Ide::onCompletionRequested);

    // Completion requests only fire while typing. Paste, undo and delete must
    // also refresh clangd, otherwise its squiggles keep describing the old
    // buffer. Debounce so every keystroke is not a full didChange message.
    auto* syncTimer = new QTimer(doc->editor);
    syncTimer->setSingleShot(true);
    syncTimer->setInterval(150);
    connect(doc->editor->document(), &QTextDocument::contentsChanged, syncTimer,
            [syncTimer] { syncTimer->start(); });
    connect(syncTimer, &QTimer::timeout, this, [this, doc] {
        if (doc->clangdOpen && m_clangd->isRunning() && !doc->path.isEmpty())
            m_clangd->changeDocument(doc->path, doc->editor->toPlainText());
    });

    connect(doc->editor, &QPlainTextEdit::cursorPositionChanged, this,
            &Ide::updateStatusCursor);
    connect(doc->editor->document(), &QTextDocument::modificationChanged, this,
            [this, doc](bool modified) {
                updateTabTitle(doc);
                if (doc->editor == m_editor)
                    updateWindowTitle();
                if (modified && m_autoSave && !doc->path.isEmpty())
                    m_autoSaveTimer->start();
            });

    updateTabTitle(doc);
    m_docTabs->setTabToolTip(index, path.isEmpty() ? doc->untitled : path);
    setActiveDoc(doc);
    return doc;
}

Ide::Doc* Ide::docAt(int index) const {
    if (!m_docTabs || index < 0 || index >= m_docTabs->count())
        return nullptr;
    auto* editor = qobject_cast<CodeEditor*>(m_docTabs->widget(index));
    return editor ? m_docs.value(editor) : nullptr;
}

Ide::Doc* Ide::activeDoc() const {
    return m_editor ? m_docs.value(m_editor) : nullptr;
}

void Ide::setActiveDoc(Doc* doc) {
    if (!doc)
        return;
    const bool changed = (doc->editor != m_editor);
    m_editor = doc->editor;
    m_highlighter = doc->highlighter;
    m_currentFile = doc->path;
    if (m_completer)
        m_completer->popup()->hide();
    m_items.clear();
    if (changed)
        m_problems->setRowCount(0); // diagnostics are per active document
    if (m_docTabs->currentWidget() != doc->editor)
        m_docTabs->setCurrentWidget(doc->editor);
    m_editor->setCompleter(m_completer);
    updateStatusCursor();
    updateWindowTitle();

    QString langName;
    languageForPath(m_langs, doc->path, &langName);
    statusBar()->showMessage("Language: " + langName, 1500);
}

void Ide::onTabChanged(int index) {
    Doc* doc = docAt(index);
    if (!doc || doc->editor == m_editor)
        return; // nullptr while a tab is being removed, or self-triggered
    setActiveDoc(doc);
}

void Ide::updateTabTitle(Doc* doc) {
    if (!doc)
        return;
    const int idx = m_docTabs->indexOf(doc->editor);
    if (idx < 0)
        return;
    const bool dirty = doc->editor->document()->isModified();
    m_docTabs->setTabText(
        idx, (doc->pinned ? "[pin] " : "") + doc->displayName() +
                 (dirty ? " *" : ""));
}

void Ide::toggleTabPinned(int index) {
    Doc* doc = docAt(index);
    if (!doc)
        return;
    doc->pinned = !doc->pinned;
    updateTabTitle(doc);
    m_docTabs->setTabToolTip(index,
                             (doc->pinned ? "Pinned: " : "") +
                                 (doc->path.isEmpty() ? doc->untitled
                                                      : doc->path));
}

void Ide::showTerminal() {
    if (!m_stack || !m_terminalOutput)
        return;
    m_stack->setCurrentIndex(1);
    auto* bottomTabs = m_terminalOutput->parentWidget()->parentWidget();
    auto* tabs = qobject_cast<QTabWidget*>(bottomTabs);
    if (tabs)
        tabs->setCurrentIndex(2);
    if (m_terminalProc->state() == QProcess::NotRunning) {
        const QString dir = m_project->root().isEmpty()
                                 ? QDir::homePath()
                                 : m_project->root();
        m_terminalProc->setWorkingDirectory(dir);
        m_terminalProc->start("/bin/bash", {"-i"});
        if (!m_terminalProc->waitForStarted(1000))
            m_terminalOutput->appendPlainText(
                "error: could not start /bin/bash\n");
    }
    m_terminalInput->setFocus();
}

void Ide::updateWindowTitle() {
    Doc* doc = activeDoc();
    if (!doc) {
        setWindowTitle("Home");
        return;
    }
    setWindowTitle(doc->displayName() + "[*]");
    setWindowModified(doc->editor->document()->isModified());
}

bool Ide::saveDoc(Doc* doc) {
    if (!doc)
        return false;
    if (doc->path.isEmpty())
        return saveDocAs(doc);
    QFile f(doc->path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "tgk editor",
                             "Could not save " + doc->path + "\n" +
                                 f.errorString());
        return false;
    }
    QTextStream out(&f);
    out << doc->editor->toPlainText();
    doc->editor->document()->setModified(false);
    updateTabTitle(doc);
    updateWindowTitle();
    statusBar()->showMessage("Saved " + doc->path, 2000);
    return true;
}

bool Ide::saveDocAs(Doc* doc) {
    if (!doc)
        return false;
    const QString suggested =
        doc->path.isEmpty() ? doc->untitled : doc->path;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save source file", suggested,
        "Source files (*.c *.cpp *.cc *.cxx *.h *.hpp *.java *.py *.js *.ts "
        "*.go *.rs *.sh *.rb *.php);;All files (*)");
    if (path.isEmpty())
        return false;

    if (!doc->path.isEmpty() && doc->path != path && doc->clangdOpen) {
        if (m_clangd->isRunning())
            m_clangd->closeDocument(doc->path);
        doc->clangdOpen = false;
    }
    doc->path = path;
    if (!saveDoc(doc))
        return false;

    doc->highlighter->setLanguage(languageForPath(m_langs, doc->path));
    updateTabTitle(doc);
    if (doc->editor == m_editor) {
        m_currentFile = doc->path;
        updateWindowTitle();
    }
    m_stack->setCurrentIndex(1);
    m_docTabs->setTabToolTip(m_docTabs->indexOf(doc->editor), doc->path);
    addRecent(path);
    return true;
}

bool Ide::maybeSaveDoc(Doc* doc) {
    if (!doc || !doc->editor->document()->isModified())
        return true;
    auto ret = QMessageBox::question(
        this, "tgk editor", "Save changes to " + doc->displayName() + "?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (ret == QMessageBox::Save)
        return saveDoc(doc);
    return ret == QMessageBox::Discard;
}

bool Ide::maybeSaveAll() {
    for (Doc* doc : m_docList) {
        if (!maybeSaveDoc(doc))
            return false;
    }
    return true;
}

void Ide::updateStatusCursor() {
    if (!m_editor) {
        m_posLabel->setText("Ln —, Col —");
        return;
    }
    QTextCursor c = m_editor->textCursor();
    m_posLabel->setText(
        QString("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.columnNumber() + 1));
}

void Ide::newFile() {
    Doc* doc = createDoc(QString(), QString());
    m_stack->setCurrentIndex(1);
    if (doc)
        doc->editor->setFocus();
    statusBar()->showMessage("New file", 2000);
}

void Ide::openFile() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Open source files", QString(),
        "Source files (*.c *.cpp *.cc *.cxx *.h *.hpp *.java *.py *.js *.ts "
        "*.go *.rs *.sh *.rb *.php);;All files (*)");
    for (const QString& path : paths)
        openFileFromPath(path);
}

void Ide::openFileFromPath(const QString& path) {
    const QFileInfo info(path);
    // Already open? Just switch to its tab — never open the same file twice.
    for (Doc* doc : m_docList) {
        if (doc->path.isEmpty())
            continue;
        if (QFileInfo(doc->path) == info) {
            m_stack->setCurrentIndex(1);
            setActiveDoc(doc);
            addRecent(doc->path);
            m_project->revealFile(doc->path);
            return;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "tgk editor",
                             "Could not open " + path + "\n" + f.errorString());
        return;
    }
    Doc* doc = createDoc(info.absoluteFilePath(),
                         QString::fromUtf8(f.readAll()));
    m_stack->setCurrentIndex(1);
    if (doc)
        doc->editor->setFocus();
    addRecent(path);
    m_project->revealFile(path);
    statusBar()->showMessage("Opened " + path, 2000);
}

void Ide::openProjectFolder(const QString& dir) {
    if (!QFileInfo(dir).isDir())
        return;
    m_project->setRoot(dir);
    m_projectDock->show();
    statusBar()->showMessage("Folder: " + dir, 3000);
}

bool Ide::saveFile() { return saveDoc(activeDoc()); }

bool Ide::saveFileAs() { return saveDocAs(activeDoc()); }

void Ide::showHome() {
    refreshRecent();
    if (m_welcome)
        m_welcome->setBackVisible(!m_docList.isEmpty());
    m_stack->setCurrentIndex(0);
    setWindowTitle("Home");
    statusBar()->showMessage(
        "Home — double-click a recent file to reopen it", 3000);
}

void Ide::showEditorPage() {
    if (m_docList.isEmpty()) {
        showHome();
        return;
    }
    m_stack->setCurrentIndex(1);
    if (m_editor)
        m_editor->setFocus();
}

void Ide::closeTab(int index) {
    Doc* doc = docAt(index);
    if (!doc)
        return;
    if (!maybeSaveDoc(doc))
        return;
    removeDoc(doc);
}

void Ide::removeDoc(Doc* doc) {
    if (!doc)
        return;
    const bool wasActive = (doc->editor == m_editor);
    if (doc->clangdOpen && !doc->path.isEmpty() && m_clangd->isRunning())
        m_clangd->closeDocument(doc->path);

    m_docs.remove(doc->editor);
    m_docList.removeAll(doc);
    const int idx = m_docTabs->indexOf(doc->editor);
    if (idx >= 0)
        m_docTabs->removeTab(idx);
    delete doc->editor; // also drops its signal connections
    delete doc;

    if (m_docTabs->count() == 0) {
        m_editor = nullptr;
        m_highlighter = nullptr;
        m_currentFile.clear();
        m_items.clear();
        showHome();
    } else if (wasActive || !m_editor) {
        setActiveDoc(docAt(m_docTabs->currentIndex()));
    }
}

void Ide::loadRecent() {
    m_recent = QSettings().value("recentFiles").toStringList();
    QStringList filtered;
    for (const QString& p : m_recent)
        if (QFileInfo::exists(p) && !filtered.contains(p))
            filtered.append(p);
    if (filtered.size() > 10)
        filtered = filtered.mid(0, 10);
    if (filtered != m_recent) {
        m_recent = filtered;
        saveRecent();
    }
}

void Ide::saveRecent() {
    QSettings s;
    s.setValue("recentFiles", m_recent);
    s.sync(); // survive crashes / forced exits
}

void Ide::addRecent(const QString& path) {
    QString p = QFileInfo(path).canonicalFilePath();
    if (p.isEmpty())
        p = QFileInfo(path).absoluteFilePath();
    m_recent.removeAll(p);
    m_recent.prepend(p);
    while (m_recent.size() > 10)
        m_recent.removeLast();
    saveRecent();
    refreshRecent();
}

void Ide::refreshRecent() {
    if (m_welcome)
        m_welcome->setRecentFiles(m_recent);
    rebuildRecentMenu();
}

void Ide::rebuildRecentMenu() {
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    if (m_recent.isEmpty()) {
        QAction* a = m_recentMenu->addAction("(no recent files)");
        a->setEnabled(false);
        return;
    }
    for (const QString& p : m_recent) {
        QFileInfo fi(p);
        QAction* a = m_recentMenu->addAction(fi.fileName() + "  —  " +
                                             fi.absolutePath());
        connect(a, &QAction::triggered, this,
                [this, p] { openFileFromPath(p); });
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction("Clear Recent Files", this, [this] {
        m_recent.clear();
        saveRecent();
        refreshRecent();
    });
}

void Ide::closeEvent(QCloseEvent* event) {
    if (maybeSaveAll())
        event->accept();
    else
        event->ignore();
}

// ---------------------------------------------------------------- build/run

void Ide::compileOnly() { startBuild(false); }
void Ide::compileAndRun() { startBuild(true); }

void Ide::startBuild(bool runAfter) {
    Doc* doc = activeDoc();
    if (!doc)
        return; // no file open
    if (doc->path.isEmpty() && !saveDocAs(doc))
        return;
    if (!saveDoc(doc))
        return;
    m_currentFile = doc->path;

    const LanguageSpec* spec = m_langs.forFile(m_currentFile);
    if (!spec) {
        QMessageBox::warning(
            this, "tgk editor",
            "Don't know how to run files like '" + m_currentFile +
                "'.\nAdd the extension to a language in "
                "File → Language Settings…\n(config: " +
                LanguageRegistry::configPath() + ")");
        return;
    }
    m_activeSpec = *spec;
    if (!m_activeSpec.confirmed && !askToolchain(m_activeSpec))
        return;

    m_runAfterBuild = runAfter;
    if (m_runProc->state() != QProcess::NotRunning)
        m_runProc->kill();

    m_buildFile = m_currentFile;
    QFileInfo fi(m_currentFile);
    m_binaryPath = LanguageRegistry::outputPathFor(m_currentFile);

    m_console->clear();
    m_problems->setRowCount(0);
    m_buildOutput.clear();
    for (Doc* d : m_docList)
        d->editor->setBuildDiagnostics({}); // fresh compiler marks per build

    if (m_activeSpec.compile.isEmpty()) {
        // interpreted language — nothing to compile
        if (runAfter)
            runProgram();
        else
            appendConsole("[Nothing to compile — " + m_activeSpec.displayName +
                          " is interpreted]\n");
        return;
    }

    QStringList parts =
        LanguageRegistry::expandArgs(m_activeSpec.compile, m_currentFile);
    if (parts.isEmpty()) {
        appendConsole("error: empty compile command\n");
        return;
    }
    appendConsole("$ " + displayCommand(parts) + "\n");
    QString prog = parts.takeFirst();
    m_buildProc->setWorkingDirectory(fi.absolutePath());
    m_buildProc->start(prog, parts);
    if (!m_buildProc->waitForStarted(3000)) {
        appendConsole("error: could not start '" + prog +
                      "' — is it installed?\nYou can change the command in "
                      "File → Language Settings…\n");
        return;
    }
}

bool Ide::askToolchain(LanguageSpec& spec) {
    QDialog dlg(this);
    dlg.setWindowTitle("tgk editor — " + spec.displayName);
    auto* form = new QFormLayout(&dlg);

    form->addRow(new QLabel(
        QString("First time running %1 files.\nConfirm the commands to use:")
            .arg(spec.displayName)));

    auto* compileEdit = new QLineEdit(spec.compile, &dlg);
    compileEdit->setPlaceholderText("(empty — interpreted language)");
    auto* runEdit = new QLineEdit(spec.run, &dlg);
    form->addRow("Compile:", compileEdit);
    form->addRow("Run:", runEdit);

    auto* remember = new QCheckBox(
        "Remember these commands (saved in tgkeditor.json)", &dlg);
    remember->setChecked(true);
    form->addRow(remember);

    form->addRow(
        new QLabel("Placeholders: {file} {dir} {name} {class} {output}", &dlg));

    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText("Run");
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    spec.compile = compileEdit->text().trimmed();
    spec.run = runEdit->text().trimmed();
    if (remember->isChecked()) {
        spec.confirmed = true;
        m_langs.update(spec);
        m_langs.save();
    }
    return true;
}

void Ide::showLanguageSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle("tgk editor — Language Settings");
    auto* layout = new QVBoxLayout(&dlg);

    layout->addWidget(new QLabel(
        "Edit the commands per language (placeholders: {file} {dir} {name} "
        "{class} {output}).\nCompile empty = interpreted. Changes are saved "
        "to:\n" + LanguageRegistry::configPath(), &dlg));

    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"Language", "Extensions", "Compile",
                                      "Run"});
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    const QVector<LanguageSpec> langs = m_langs.all();
    table->setRowCount(langs.size());
    for (int i = 0; i < langs.size(); ++i) {
        const LanguageSpec& s = langs.at(i);
        auto* nameItem =
            new QTableWidgetItem(s.displayName + (s.confirmed ? "" : " (ask)"));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        auto* extItem = new QTableWidgetItem(s.extensions.join(" "));
        extItem->setFlags(extItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 0, nameItem);
        table->setItem(i, 1, extItem);
        table->setItem(i, 2, new QTableWidgetItem(s.compile));
        table->setItem(i, 3, new QTableWidgetItem(s.run));
    }
    layout->addWidget(table);

    auto* bb = new QDialogButtonBox(&dlg);
    QPushButton* resetBtn = bb->addButton("Reset Defaults",
                                          QDialogButtonBox::ResetRole);
    bb->addButton(QDialogButtonBox::Save);
    bb->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(bb);

    connect(resetBtn, &QPushButton::clicked, &dlg, [this, &dlg] {
        if (QMessageBox::question(&dlg, "tgk editor",
                                  "Reset all languages to built-in defaults?")
            == QMessageBox::Yes) {
            m_langs.resetDefaults();
            dlg.reject();
        }
    });
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        for (int i = 0; i < table->rowCount() && i < langs.size(); ++i) {
            LanguageSpec s = langs.at(i);
            s.compile = table->item(i, 2)->text().trimmed();
            s.run = table->item(i, 3)->text().trimmed();
            s.confirmed = true; // user reviewed them — stop asking
            m_langs.update(s);
        }
        m_langs.save();
        statusBar()->showMessage("Language settings saved", 2000);
    }
}

void Ide::onBuildFinished(int exitCode, QProcess::ExitStatus) {
    QString output = m_buildOutput;
    if (!output.trimmed().isEmpty()) {
        parseBuildErrors(output);
    }

    if (exitCode != 0) {
        appendConsole("\n[Build failed — see Problems tab]\n");
        statusBar()->showMessage("Build failed", 4000);
        m_problems->raise();
        return;
    }
    appendConsole("[Build OK]\n");
    statusBar()->showMessage("Build succeeded", 2000);
    if (m_runAfterBuild)
        runProgram();
}

void Ide::runProgram() {
    QFileInfo fi(m_buildFile);
    m_console->appendPlainText("\n--- running " + fi.fileName() + " ---\n");

    QStringList parts =
        LanguageRegistry::expandArgs(m_activeSpec.run, m_buildFile);
    if (parts.isEmpty()) {
        appendConsole("[No run command configured for " +
                      m_activeSpec.displayName + "]\n");
        return;
    }
    appendConsole("$ " + displayCommand(parts) + "\n");
    QString prog = parts.takeFirst();
    m_runProc->setWorkingDirectory(fi.absolutePath());
    m_runProc->start(prog, parts);
    if (!m_runProc->waitForStarted(3000)) {
        appendConsole("error: could not start '" + prog +
                      "' — is it installed?\nYou can change the command in "
                      "File → Language Settings…\n");
        m_inputLine->setEnabled(false);
        return;
    }
    m_inputLine->setEnabled(true);
    m_inputLine->setFocus();
}

void Ide::onRunFinished(int exitCode, QProcess::ExitStatus) {
    QString out = QString::fromUtf8(m_runProc->readAllStandardOutput());
    if (!out.isEmpty())
        appendConsole(out);
    appendConsole(QString("\n[Process exited with code %1]\n").arg(exitCode));
    m_inputLine->setEnabled(false);
    if (m_editor)
        m_editor->setFocus();
}

void Ide::stopProgram() {
    if (m_runProc->state() != QProcess::NotRunning) {
        m_runProc->kill();
        appendConsole("\n[Program stopped]\n");
    }
}

void Ide::toggleComment() {
    if (!m_editor || !m_highlighter)
        return;
    QTextCursor sel = m_editor->textCursor();
    int startLine = sel.blockNumber();
    int endLine = sel.blockNumber();
    if (sel.hasSelection() && sel.positionInBlock() == 0 &&
        sel.position() != sel.anchor())
        --endLine; // selection ends at column 0: don't touch that line

    const QString prefix = m_highlighter->lineCommentPrefix(); // "//" or "#"

    // Uncomment only if every non-blank line in range is already commented
    bool allCommented = true;
    for (int n = startLine; n <= endLine; ++n) {
        QString t = m_editor->document()->findBlockByNumber(n).text();
        if (t.trimmed().isEmpty())
            continue;
        if (!t.trimmed().startsWith(prefix)) {
            allCommented = false;
            break;
        }
    }

    QTextCursor edit(m_editor->document());
    edit.beginEditBlock();
    for (int n = startLine; n <= endLine; ++n) {
        QTextBlock block = m_editor->document()->findBlockByNumber(n);
        if (!block.isValid())
            continue;
        QString t = block.text();
        if (t.trimmed().isEmpty())
            continue;

        int indent = 0;
        while (indent < t.length() && t.at(indent).isSpace())
            ++indent;

        QTextCursor c(block);
        if (allCommented) {
            int pos = t.indexOf(prefix, indent);
            if (pos < 0)
                pos = indent;
            c.setPosition(pos);
            int len = prefix.length();
            if (t.mid(pos + prefix.length(), 1) == " ")
                ++len; // also eat the space: "// code" -> "code"
            c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, len);
            c.removeSelectedText();
        } else {
            c.setPosition(indent);
            c.insertText(prefix + " ");
        }
    }
    edit.endEditBlock();
}

void Ide::sendProgramInput() {
    if (m_runProc->state() == QProcess::NotRunning)
        return;
    m_runProc->write(m_inputLine->text().toUtf8() + "\n");
    m_inputLine->clear();
}

void Ide::appendConsole(const QString& text) {
    m_console->moveCursor(QTextCursor::End);
    m_console->insertPlainText(text);
    m_console->moveCursor(QTextCursor::End);
}

void Ide::parseBuildErrors(const QString& output) {
    // gcc / g++ / clang / rustc:  file.cpp:12:5: error: msg
    static const QRegularExpression errRe(
        "^(.+?):(\\d+):(\\d+):\\s+(error|warning|note):\\s+(.*)$");
    // javac:  Main.java:12: error: msg
    static const QRegularExpression javacRe(
        "^(.+?):(\\d+):\\s+(error|warning):\\s+(.*)$");
    // python traceback:  File "/path/x.py", line 12
    static const QRegularExpression pyRe(
        "File \"(.+)\", line (\\d+)");

    // Map the file name printed by the toolchain back to an open tab so the
    // error can be underlined in the editor.
    auto docFor = [this](const QString& printed) -> Doc* {
        const QFileInfo printedInfo(printed);
        const QString absolute =
            printedInfo.isAbsolute()
                ? printedInfo.absoluteFilePath()
                : QFileInfo(QFileInfo(m_buildFile).absolutePath() + "/" +
                            printed).absoluteFilePath();
        for (Doc* d : m_docList) {
            if (d->path.isEmpty())
                continue;
            if (QFileInfo(d->path).absoluteFilePath() == absolute)
                return d;
            if (!printedInfo.isAbsolute() &&
                QFileInfo(d->path).fileName() == printedInfo.fileName())
                return d;
        }
        return nullptr;
    };

    QHash<Doc*, QVector<DiagnosticMark>> marks;
    auto addMark = [&](const QString& printed, int line, int col,
                       int severity, const QString& message, int endCol) {
        Doc* d = docFor(printed);
        if (!d)
            return;
        DiagnosticMark m;
        m.line = qMax(0, line - 1);
        m.col = qMax(0, col - 1);
        m.endLine = m.line;
        m.endCol = endCol;
        m.severity = severity;
        m.message = message;
        marks[d].append(m);
    };

    for (const QString& rawLine : output.split('\n')) {
        QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        auto m = errRe.match(line);
        if (m.hasMatch()) {
            const QString kind = m.captured(4);
            const int severity =
                kind == "error" ? 1 : kind == "warning" ? 2 : 3;
            addProblemRow(kind, m.captured(5),
                          QFileInfo(m.captured(1)).fileName(),
                          m.captured(2).toInt(), m.captured(3).toInt());
            addMark(m.captured(1), m.captured(2).toInt(),
                    m.captured(3).toInt(), severity, m.captured(5), 0);
            continue;
        }
        auto j = javacRe.match(line);
        if (j.hasMatch()) {
            addProblemRow(j.captured(3), j.captured(4),
                          QFileInfo(j.captured(1)).fileName(),
                          j.captured(2).toInt(), 1);
            addMark(j.captured(1), j.captured(2).toInt(), 1,
                    j.captured(3) == "error" ? 1 : 2, j.captured(4), -1);
            continue;
        }
        auto p = pyRe.match(line);
        if (p.hasMatch()) {
            addProblemRow("error", "Python traceback — see Output pane",
                          QFileInfo(p.captured(1)).fileName(),
                          p.captured(2).toInt(), 1);
            addMark(p.captured(1), p.captured(2).toInt(), 1, 1,
                    "Python traceback — see Output pane", -1);
        }
    }

    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
        it.key()->editor->setBuildDiagnostics(it.value());
}

// ------------------------------------------------------------- completion

void Ide::syncDocument() {
    Doc* doc = activeDoc();
    if (!doc || doc->path.isEmpty())
        return;
    if (doc->clangdOpen && m_clangd->isRunning())
        m_clangd->changeDocument(doc->path, doc->editor->toPlainText());
}

// Editors that get good clangd results (VS Code, Qt Creator, Emacs) feed it a
// compile_commands.json so IntelliSense sees the same -I/-D/-std flags as the
// compiler. When the project has none, build a small one in the cache from the
// language's configured compile command. This stops false "file not found"
// squiggles on #include lines for headers the real build resolves fine.
QString Ide::prepareCompileCommands(const QString& root) {
    if (root.isEmpty())
        return {};

    // Never shadow a compilation database the project already provides.
    for (QDir dir(root);;) {
        if (QFile::exists(dir.filePath("compile_commands.json")))
            return {};
        if (!dir.cdUp())
            break;
    }

    QJsonArray commands;
    for (Doc* doc : m_docList) {
        if (doc->path.isEmpty())
            continue;
        const LanguageSpec* spec = m_langs.forFile(doc->path);
        if (!spec || (spec->id != "cpp" && spec->id != "c") ||
            spec->compile.isEmpty())
            continue;

        QStringList args = LanguageRegistry::expandArgs(spec->compile, doc->path);
        if (args.isEmpty())
            continue;
        // clangd only needs front-end flags; keep "-o out" out of the database.
        for (int i = 0; i + 1 < args.size();) {
            if (args.at(i) == "-o") {
                args.removeAt(i);
                args.removeAt(i);
            } else {
                ++i;
            }
        }

        const QFileInfo fi(doc->path);
        QJsonObject entry;
        entry["directory"] = fi.absolutePath();
        entry["file"] = fi.absoluteFilePath();
        entry["arguments"] = QJsonArray::fromStringList(args);
        commands.append(entry);
    }
    if (commands.isEmpty())
        return {};

    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QByteArray hash =
        QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha1)
            .toHex()
            .left(12);
    const QString dir = base + "/clangd-" + QString::fromLatin1(hash);
    if (!QDir().mkpath(dir))
        return {};

    QFile file(dir + "/compile_commands.json");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(QJsonDocument(commands).toJson(QJsonDocument::Indented));
    return dir;
}

void Ide::onCompletionRequested() {
    Doc* doc = activeDoc();
    if (!doc || !m_editor)
        return;
    if (m_highlighter->language() != CppHighlighter::Lang::Cpp) {
        m_intelLabel->setText("IntelliSense: C/C++ only");
        return;
    }
    if (doc->path.isEmpty()) {
        m_intelLabel->setText("IntelliSense: save the file first");
        return;
    }

    QTextCursor cur = m_editor->textCursor();
    QString lineText = cur.block().text().left(cur.columnNumber());
    // start of the word being typed (letters, digits, _, :, ~)
    int i = lineText.length();
    while (i > 0) {
        QChar c = lineText.at(i - 1);
        if (c.isLetterOrNumber() || c == '_' || c == ':' || c == '~')
            --i;
        else
            break;
    }
    m_prefixStart = cur.block().position() + i;

    if (!m_clangd->isRunning()) {
        QFileInfo fi(doc->path);
        m_clangdRoot = fi.absolutePath();
        m_clangdCompileDir = prepareCompileCommands(m_clangdRoot);
        if (!m_clangd->start(m_clangdRoot, m_clangdCompileDir)) {
            m_intelLabel->setText("IntelliSense: clangd not found");
            return;
        }
        m_intelLabel->setText("IntelliSense: starting…");
    }

    if (!doc->clangdOpen) {
        // Opening a sibling file may add flags to the compilation database.
        if (!m_clangdCompileDir.isEmpty() &&
            QFileInfo(doc->path).absolutePath() == m_clangdRoot)
            prepareCompileCommands(m_clangdRoot);
        m_clangd->openDocument(doc->path, doc->editor->toPlainText());
        doc->clangdOpen = true;
        m_intelLabel->setText("IntelliSense: on");
    } else {
        syncDocument();
    }

    // Remember which editor asked: the user may switch tabs while clangd works.
    QPointer<CodeEditor> askingEditor = m_editor;
    m_clangd->completion(doc->path, cur.blockNumber(), cur.columnNumber(),
                         [this, askingEditor](
                             bool ok, const QVector<ClangdItem>& items) {
                             QMetaObject::invokeMethod(
                                 this,
                                 [this, askingEditor, ok, items] {
                                     if (askingEditor != m_editor)
                                         return; // switched tabs meanwhile
                                     onCompletionReady(ok, items);
                                 },
                                 Qt::QueuedConnection);
                         });
}

void Ide::onCompletionReady(bool ok, const QVector<ClangdItem>& items) {
    // ignore stale replies if the cursor already moved on
    if (!m_editor || !m_completer)
        return;
    QTextCursor cur = m_editor->textCursor();
    if (!ok) {
        m_completer->popup()->hide();
        return;
    }

    m_items = items;
    QStringList labels;
    labels.reserve(items.size());
    for (const ClangdItem& it : items) {
        QString label = it.label.trimmed();
        if (it.detail.isEmpty())
            labels << label;
        else
            labels << label + "  ·  " + it.detail;
    }
    m_completerModel->setStringList(labels);

    if (items.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    QString prefix = m_editor->toPlainText()
                         .mid(m_prefixStart,
                              cur.position() - m_prefixStart);
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0) {
        // nothing matches the typed prefix — show everything (VS Code style)
        m_completer->setCompletionPrefix(QString());
    }

    QRect cr = m_editor->cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                m_completer->popup()->verticalScrollBar()->sizeHint().width() +
                30);
    m_completer->complete(cr);
}

void Ide::insertCompletion(const QModelIndex& index) {
    if (!index.isValid() || index.row() >= m_items.size() || !m_editor)
        return;
    const ClangdItem& item = m_items.at(index.row());
    QTextCursor cur = m_editor->textCursor();

    auto posOf = [this](int line, int col) -> int {
        QTextBlock b = m_editor->document()->findBlockByNumber(line);
        if (!b.isValid())
            return -1;
        return b.position() + col;
    };

    if (item.hasRange) {
        int s = posOf(item.startLine, item.startCol);
        int e = posOf(item.endLine, item.endCol);
        if (s >= 0 && e >= 0 && e >= s) {
            cur.setPosition(s);
            cur.setPosition(e, QTextCursor::KeepAnchor);
        }
    } else {
        cur.setPosition(m_prefixStart);
        cur.setPosition(m_editor->textCursor().position(),
                        QTextCursor::KeepAnchor);
    }
    cur.insertText(item.insertText);
    m_editor->setTextCursor(cur);
}

// ------------------------------------------------------------- diagnostics

void Ide::addProblemRow(const QString& severity, const QString& message,
                        const QString& file, int line, int col) {
    int row = m_problems->rowCount();
    m_problems->insertRow(row);
    m_problems->setItem(row, 0, new QTableWidgetItem(severity));
    m_problems->setItem(row, 1, new QTableWidgetItem(message));
    m_problems->setItem(row, 2, new QTableWidgetItem(file));
    m_problems->setItem(row, 3, new QTableWidgetItem(QString::number(line)));
    m_problems->setItem(row, 4, new QTableWidgetItem(QString::number(col)));
}

void Ide::onDiagnostics(const QString& path,
                        const QVector<ClangdDiagnostic>& diags) {
    Doc* target = nullptr;
    for (Doc* d : m_docList) {
        if (d->path == path) {
            target = d;
            break;
        }
    }
    if (!target)
        return;

    QVector<DiagnosticMark> marks;
    marks.reserve(diags.size());
    for (const ClangdDiagnostic& d : diags) {
        DiagnosticMark m;
        m.line = d.line;
        m.col = d.col;
        m.endLine = d.endLine;
        m.endCol = d.endCol;
        m.severity = d.severity == 1 ? 1 : d.severity == 2 ? 2 : 3;
        const QTextBlock line =
            target->editor->document()->findBlockByNumber(d.line);
        m.message = augmentDiagnostic(
            d.message, line.isValid() ? line.text() : QString());
        marks.append(m);
    }
    target->editor->setDiagnostics(marks); // red squiggles in that tab

    // The Problems table keeps showing the active document.
    Doc* doc = activeDoc();
    if (!doc || doc != target)
        return;
    // rebuild rows coming from clangd (keep rows produced by the compiler:
    // they have an absolute file name in col 2 which never matches here —
    // simplest reliable approach: wipe and refill)
    m_problems->setRowCount(0);
    int errors = 0;
    for (int i = 0; i < diags.size(); ++i) {
        const ClangdDiagnostic& d = diags.at(i);
        QString sev = d.severity == 1   ? "error"
                      : d.severity == 2 ? "warning"
                                        : "note";
        if (d.severity == 1)
            ++errors;
        addProblemRow(sev, marks.at(i).message, QFileInfo(path).fileName(),
                      d.line + 1, d.col + 1);
    }
    m_intelLabel->setText(errors > 0
                              ? QString("IntelliSense: on — %1 error(s)").arg(errors)
                              : "IntelliSense: on");
}

void Ide::onCellDoubleClicked(int row, int) {
    auto* fItem = m_problems->item(row, 2);
    auto* lItem = m_problems->item(row, 3);
    auto* cItem = m_problems->item(row, 4);
    if (!fItem || !lItem)
        return;
    jumpTo(fItem->text(), lItem->text().toInt(),
           cItem ? cItem->text().toInt() : 1);
}

void Ide::jumpTo(const QString& file, int line, int col) {
    QString path = file;
    if (!QFileInfo(path).isAbsolute()) {
        if (m_currentFile.isEmpty())
            return;
        path = QFileInfo(m_currentFile).absolutePath() + "/" + file;
    }
    if (QFileInfo(path).fileName() != QFileInfo(m_currentFile).fileName() &&
        QFileInfo(path).exists())
        openFileFromPath(path);

    if (!m_editor)
        return;
    QTextBlock block = m_editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    QTextCursor cur(block);
    cur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                     qMax(0, col - 1));
    m_editor->setTextCursor(cur);
    m_editor->centerCursor();
}
