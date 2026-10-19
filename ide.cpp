#include "ide.h"
#include "codeeditor.h"
#include "highlighter.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QScrollBar>
#include <QSettings>
#include <QAbstractItemView>
#include <QStatusBar>
#include <QStringListModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

Ide::Ide(QWidget* parent) : QMainWindow(parent) {
    m_editor = new CodeEditor(this);
    m_highlighter = new CppHighlighter(m_editor->document());
    m_langs.load(); // built-ins + ~/.config/tgk/tgkeditor.json

    m_completer = new QCompleter(this);
    m_completerModel = new QStringListModel(this);
    m_completer->setModel(m_completerModel);
    m_editor->setCompleter(m_completer);
    connect(m_completer, QOverload<const QModelIndex&>::of(&QCompleter::activated),
            this, &Ide::insertCompletion);
    connect(m_editor, &CodeEditor::completionRequested, this,
            &Ide::onCompletionRequested);

    m_clangd = new ClangdClient(this);
    connect(m_clangd, &ClangdClient::diagnostics, this, &Ide::onDiagnostics);
    connect(m_clangd, &ClangdClient::serverDied, this, [this] {
        m_intelLabel->setText("IntelliSense: offline");
        m_openPath.clear();
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

    setupUi();
    setupActions();

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(11);
    m_editor->setFont(mono);

    applyTheme(findTheme(QSettings().value("theme", "classic").toString()));

    resize(1200, 800);
    setWindowTitle(defaultTitle());
    statusBar()->showMessage("Ready — F9 compiles & runs, Ctrl+Space completes");
}

void Ide::applyTheme(const Theme& t) {
    m_editor->setTheme(t);
    m_highlighter->setTheme(t);

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

    bottomLayout->addWidget(tabs);

    splitter->addWidget(m_editor);
    splitter->addWidget(bottom);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_posLabel = new QLabel("Ln 1, Col 1", this);
    m_intelLabel = new QLabel("IntelliSense: off", this);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_intelLabel);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this,
            &Ide::updateStatusCursor);
}

void Ide::setupActions() {
    // File menu
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&New", this, &Ide::newFile, QKeySequence::New);
    fileMenu->addAction("&Open…", this, &Ide::openFile, QKeySequence::Open);
    fileMenu->addAction("&Save", this, &Ide::saveFile, QKeySequence::Save);
    fileMenu->addAction("Save &As…", this, &Ide::saveFileAs,
                        QKeySequence::SaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction("&Language Settings…", this,
                        &Ide::showLanguageSettings);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", qApp, &QApplication::quit,
                        QKeySequence::Quit);

    // Edit menu
    QMenu* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("&Toggle Comment", this, &Ide::toggleComment,
                        QKeySequence(Qt::CTRL | Qt::Key_Backslash));

    // Build menu
    QMenu* buildMenu = menuBar()->addMenu("&Build");
    buildMenu->addAction("&Compile (F8)", this, &Ide::compileOnly,
                         QKeySequence(Qt::Key_F8));
    buildMenu->addAction("&Compile & Run (F9)", this, &Ide::compileAndRun,
                         QKeySequence(Qt::Key_F9));
    buildMenu->addAction("&Stop program", this, &Ide::stopProgram,
                         QKeySequence(Qt::Key_F10));

    // View menu — themes
    QMenu* viewMenu = menuBar()->addMenu("&View");
    QActionGroup* themeGroup = new QActionGroup(this);
    QString currentTheme =
        QSettings().value("theme", "classic").toString();
    for (const Theme& t : builtinThemes()) {
        QAction* a = viewMenu->addAction(t.name);
        a->setCheckable(true);
        a->setActionGroup(themeGroup);
        a->setChecked(t.id == currentTheme);
        connect(a, &QAction::triggered, this, [this, t] {
            applyTheme(t);
            QSettings().setValue("theme", t.id);
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

    // Toolbar — each button shows its keybinding underneath
    QToolBar* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    tb->addAction("New\n(Ctrl+N)", this, &Ide::newFile);
    tb->addAction("Open\n(Ctrl+O)", this, &Ide::openFile);
    tb->addAction("Save\n(Ctrl+S)", this, &Ide::saveFile);
    tb->addSeparator();
    tb->addAction("Comment\n(Ctrl+\\)", this, &Ide::toggleComment);
    tb->addSeparator();
    tb->addAction("Compile\n(F8)", this, &Ide::compileOnly);
    tb->addAction("▶ Run\n(F9)", this, &Ide::compileAndRun);
    tb->addAction("■ Stop\n(F10)", this, &Ide::stopProgram);
}

QString Ide::defaultTitle() const {
    QString name = m_currentFile.isEmpty()
                       ? "untitled.cpp"
                       : QFileInfo(m_currentFile).fileName();
    return name + "[*] — tgk editor";
}

void Ide::updateStatusCursor() {
    QTextCursor c = m_editor->textCursor();
    m_posLabel->setText(
        QString("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.columnNumber() + 1));
}

void Ide::newFile() {
    if (!maybeSave())
        return;
    m_editor->clear();
    m_currentFile.clear();
    setWindowTitle(defaultTitle());
    m_editor->document()->setModified(false);
    updateLanguageForFile();
    statusBar()->showMessage("New file", 2000);
}

void Ide::openFile() {
    if (!maybeSave())
        return;
    QString path = QFileDialog::getOpenFileName(
        this, "Open source file", QString(),
        "C/C++ files (*.c *.cpp *.cc *.cxx *.h *.hpp);;All files (*)");
    if (!path.isEmpty())
        openFileFromPath(path);
}

void Ide::openFileFromPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "tgk editor",
                             "Could not open " + path + "\n" + f.errorString());
        return;
    }
    m_editor->setPlainText(QString::fromUtf8(f.readAll()));
    m_currentFile = path;
    m_editor->document()->setModified(false);
    setWindowTitle(defaultTitle());
    updateLanguageForFile();
    statusBar()->showMessage("Opened " + path, 2000);
}

void Ide::updateLanguageForFile() {
    CppHighlighter::Lang lang = CppHighlighter::Lang::Cpp;
    const LanguageSpec* spec = m_langs.forFile(m_currentFile);
    if (m_currentFile.isEmpty()) {
        statusBar()->showMessage("Language: C++ (default for new files)", 2000);
    } else if (spec) {
        if (spec->id == "cpp" || spec->id == "c")
            lang = CppHighlighter::Lang::Cpp;
        else if (spec->id == "java")
            lang = CppHighlighter::Lang::Java;
        else if (spec->id == "python")
            lang = CppHighlighter::Lang::Python;
        statusBar()->showMessage("Language: " + spec->displayName, 2000);
    } else {
        statusBar()->showMessage("Unknown file type — plain text mode", 2000);
    }
    m_highlighter->setLanguage(lang);
}

bool Ide::saveFile() {
    if (m_currentFile.isEmpty())
        return saveFileAs();
    QFile f(m_currentFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "tgk editor",
                             "Could not save " + m_currentFile + "\n" +
                                 f.errorString());
        return false;
    }
    QTextStream out(&f);
    out << m_editor->toPlainText();
    m_editor->document()->setModified(false);
    statusBar()->showMessage("Saved " + m_currentFile, 2000);
    return true;
}

bool Ide::saveFileAs() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save source file", "untitled.cpp",
        "C/C++ files (*.c *.cpp *.cc *.cxx *.h *.hpp)");
    if (path.isEmpty())
        return false;
    m_currentFile = path;
    setWindowTitle(defaultTitle());
    return saveFile();
}

bool Ide::maybeSave() {
    if (!m_editor->document()->isModified())
        return true;
    auto ret = QMessageBox::question(
        this, "tgk editor", "Save changes to " + defaultTitle() + "?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save)
        return saveFile();
    return ret == QMessageBox::Discard;
}

void Ide::closeEvent(QCloseEvent* event) {
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

// ---------------------------------------------------------------- build/run

void Ide::compileOnly() { startBuild(false); }
void Ide::compileAndRun() { startBuild(true); }

void Ide::startBuild(bool runAfter) {
    if (m_currentFile.isEmpty() && !saveFileAs())
        return;
    if (!saveFile())
        return;

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

    QFileInfo fi(m_currentFile);
    m_binaryPath = LanguageRegistry::outputPathFor(m_currentFile);

    m_console->clear();
    m_problems->setRowCount(0);
    m_buildOutput.clear();

    if (m_activeSpec.compile.isEmpty()) {
        // interpreted language — nothing to compile
        if (runAfter)
            runProgram();
        else
            appendConsole("[Nothing to compile — " + m_activeSpec.displayName +
                          " is interpreted]\n");
        return;
    }

    QString cmd =
        LanguageRegistry::expand(m_activeSpec.compile, m_currentFile);
    appendConsole("$ " + cmd + "\n");
    QStringList parts = QProcess::splitCommand(cmd);
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
    QFileInfo fi(m_currentFile);
    m_console->appendPlainText("\n--- running " + fi.fileName() + " ---\n");

    QString cmd = LanguageRegistry::expand(m_activeSpec.run, m_currentFile);
    if (cmd.trimmed().isEmpty()) {
        appendConsole("[No run command configured for " +
                      m_activeSpec.displayName + "]\n");
        return;
    }
    appendConsole("$ " + cmd + "\n");
    QStringList parts = QProcess::splitCommand(cmd);
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
    m_editor->setFocus();
}

void Ide::stopProgram() {
    if (m_runProc->state() != QProcess::NotRunning) {
        m_runProc->kill();
        appendConsole("\n[Program stopped]\n");
    }
}

void Ide::toggleComment() {
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

    for (const QString& rawLine : output.split('\n')) {
        QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        auto m = errRe.match(line);
        if (m.hasMatch()) {
            addProblemRow(m.captured(4), m.captured(5),
                          QFileInfo(m.captured(1)).fileName(),
                          m.captured(2).toInt(), m.captured(3).toInt());
            continue;
        }
        auto j = javacRe.match(line);
        if (j.hasMatch()) {
            addProblemRow(j.captured(3), j.captured(4),
                          QFileInfo(j.captured(1)).fileName(),
                          j.captured(2).toInt(), 1);
            continue;
        }
        auto p = pyRe.match(line);
        if (p.hasMatch()) {
            addProblemRow("error", "Python traceback — see Output pane",
                          QFileInfo(p.captured(1)).fileName(),
                          p.captured(2).toInt(), 1);
        }
    }
}

// ------------------------------------------------------------- completion

void Ide::syncDocument() {
    if (m_currentFile.isEmpty())
        return;
    if (m_clangd->isRunning())
        m_clangd->changeDocument(m_currentFile, m_editor->toPlainText());
}

void Ide::onCompletionRequested() {
    if (m_highlighter->language() != CppHighlighter::Lang::Cpp) {
        m_intelLabel->setText("IntelliSense: C/C++ only");
        return;
    }
    if (m_currentFile.isEmpty()) {
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
        QFileInfo fi(m_currentFile);
        if (!m_clangd->start(fi.absolutePath())) {
            m_intelLabel->setText("IntelliSense: clangd not found");
            return;
        }
        m_intelLabel->setText("IntelliSense: starting…");
        m_openPath.clear();
    }

    if (m_openPath != m_currentFile) {
        if (!m_openPath.isEmpty())
            m_clangd->closeDocument(m_openPath);
        m_clangd->openDocument(m_currentFile, m_editor->toPlainText());
        m_openPath = m_currentFile;
        m_intelLabel->setText("IntelliSense: on");
    } else {
        syncDocument();
    }

    m_clangd->completion(m_currentFile, cur.blockNumber(),
                         cur.columnNumber(),
                         [this](bool ok, const QVector<ClangdItem>& items) {
                             QMetaObject::invokeMethod(
                                 this, [this, ok, items] {
                                     onCompletionReady(ok, items);
                                 },
                                 Qt::QueuedConnection);
                         });
}

void Ide::onCompletionReady(bool ok, const QVector<ClangdItem>& items) {
    // ignore stale replies if the cursor already moved on
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
    if (!index.isValid() || index.row() >= m_items.size())
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
    if (path != m_currentFile)
        return;
    // rebuild rows coming from clangd (keep rows produced by the compiler:
    // they have an absolute file name in col 2 which never matches here —
    // simplest reliable approach: wipe and refill)
    m_problems->setRowCount(0);
    int errors = 0;
    for (const ClangdDiagnostic& d : diags) {
        QString sev = d.severity == 1   ? "error"
                      : d.severity == 2 ? "warning"
                                        : "note";
        if (d.severity == 1)
            ++errors;
        addProblemRow(sev, d.message, QFileInfo(path).fileName(), d.line + 1,
                      d.col + 1);
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

    QTextBlock block = m_editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    QTextCursor cur(block);
    cur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                     qMax(0, col - 1));
    m_editor->setTextCursor(cur);
    m_editor->centerCursor();
}



