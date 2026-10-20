// Headless checks for the multi-tab document handling.
// Run with: QT_QPA_PLATFORM=offscreen ./build/tabs_test

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextDocument>

#include <cstdio>

#include "codeeditor.h"
#include "ide.h"
#include "languages.h"

static int failures = 0;

static void check(bool ok, const char* what) {
    if (ok)
        fprintf(stderr, "ok   %s\n", what);
    else {
        fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

// Ask the tab widget to close a tab exactly like the close button does.
static void closeTab(QTabWidget* tabs, int index) {
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, index));
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("tgkeditor_test"); // keep real recents untouched
    app.setOrganizationName("tgk");

    const QString dir = QDir::tempPath() + "/tgk_tabs_test";
    QDir().mkpath(dir);
    const QString f1 = dir + "/one.cpp";
    const QString f2 = dir + "/two.py";
    const QString f3 = dir + "/three.java";
    for (const QString& p : {f1, f2, f3}) {
        QFile f(p);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("// test\n");
    }

    Ide w;
    w.show();

    auto* tabs = w.findChild<QTabWidget*>();
    auto* stack = w.findChild<QStackedWidget*>();
    check(tabs != nullptr, "tab widget exists");
    check(stack != nullptr, "stacked page exists");
    check(tabs->count() == 0, "starts with no tabs");

    w.openFileFromPath(f1);
    check(tabs->count() == 1, "opening one file creates one tab");
    check(tabs->tabText(0).contains("one.cpp"), "tab shows the file name");

    w.openFileFromPath(f2);
    w.openFileFromPath(f3);
    check(tabs->count() == 3, "three files -> three tabs");
    check(tabs->currentIndex() == 2, "newest file is active");

    // Re-opening an already open file must focus it, not duplicate it.
    w.openFileFromPath(f1);
    check(tabs->count() == 3, "re-opening a file does not duplicate the tab");
    check(tabs->currentIndex() == 0, "re-opening a file switches to its tab");

    // Modified marker on the tab text.
    auto* editor = qobject_cast<CodeEditor*>(tabs->widget(0));
    check(editor != nullptr, "tab page is an editor");
    editor->document()->setModified(true);
    check(tabs->tabText(0).contains("*"), "modified tab shows a marker");
    editor->document()->setModified(false);

    // Ctrl+Tab equivalent: tabs can be switched programmatically.
    tabs->setCurrentIndex(1);
    check(tabs->currentIndex() == 1, "tabs can be switched");

    // --- typing behaviour: auto pairs + indentation --------------------
    editor->setTabWidth(4, false);
    auto sendKey = [&](int key, const QString& text = QString(),
                       Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QKeyEvent ev(QEvent::KeyPress, key, mods, text);
        QApplication::sendEvent(editor, &ev);
    };
    auto typeChar = [&](QChar ch) { sendKey(ch.unicode(), QString(ch)); };
    auto setText = [&](const QString& s) {
        editor->setPlainText(s);
        QTextCursor c = editor->textCursor();
        c.movePosition(QTextCursor::End);
        editor->setTextCursor(c);
    };

    editor->clear();
    typeChar('(');
    check(editor->toPlainText() == "()", "typing ( inserts the closing )");
    check(editor->textCursor().position() == 1, "cursor sits inside the pair");
    typeChar(')');
    check(editor->toPlainText() == "()", "typing ) steps over the existing one");
    check(editor->textCursor().position() == 2, "cursor moved past the pair");

    editor->clear();
    typeChar('[');
    typeChar(']');
    check(editor->toPlainText() == "[]", "typing ] also steps over");
    QTextCursor mid = editor->textCursor();
    mid.setPosition(1);
    editor->setTextCursor(mid);
    sendKey(Qt::Key_Backspace);
    check(editor->toPlainText().isEmpty(), "Backspace between [] deletes both");

    setText(QString());
    sendKey(Qt::Key_Tab);
    check(editor->toPlainText() == "    ", "Tab inserts 4 spaces");

    setText("void f() {");
    sendKey(Qt::Key_Return);
    typeChar('x');
    check(editor->toPlainText() == "void f() {\n    x",
          "Enter auto-indents inside a block");
    check(editor->textCursor().blockNumber() == 1, "new line is the current one");

    editor->clear();
    typeChar('{');
    sendKey(Qt::Key_Return);
    check(editor->toPlainText() == "{\n    \n}",
          "Enter between {} creates an aligned empty line");
    check(editor->textCursor().blockNumber() == 1, "cursor on the middle line");

    setText("    ");
    typeChar('}');
    check(editor->toPlainText() == "}", "typing } dedents an empty line");

    setText("a\nb");
    QTextCursor all = editor->textCursor();
    all.select(QTextCursor::Document);
    editor->setTextCursor(all);
    sendKey(Qt::Key_Tab);
    check(editor->toPlainText() == "    a\n    b", "Tab indents selected lines");
    all = editor->textCursor();
    all.select(QTextCursor::Document);
    editor->setTextCursor(all);
    sendKey(Qt::Key_Backtab, QString(), Qt::ShiftModifier);
    check(editor->toPlainText() == "a\nb", "Shift+Tab outdents selected lines");

    editor->setTabWidth(3, false);
    editor->clear();
    sendKey(Qt::Key_Tab);
    check(editor->toPlainText() == "   ", "tab width is configurable (3 spaces)");

    // --- auto save ----------------------------------------------------
    auto* autoSaveBox = w.findChild<QCheckBox*>();
    check(autoSaveBox != nullptr, "auto-save checkbox exists");
    if (autoSaveBox) {
        autoSaveBox->setChecked(true);
        tabs->setCurrentIndex(0);
        auto* ed0 = qobject_cast<CodeEditor*>(tabs->widget(0));
        QTextCursor c = ed0->textCursor();
        c.select(QTextCursor::Document);
        c.insertText("auto saved\n");
        ed0->setTextCursor(c);
        check(ed0->document()->isModified(), "edit marks the file modified");
        QMetaObject::invokeMethod(&w, "autoSaveNow", Qt::DirectConnection);
        QFile saved(f1);
        saved.open(QIODevice::ReadOnly | QIODevice::Text);
        check(QString::fromUtf8(saved.readAll()) == "auto saved\n",
              "auto-save writes the file to disk");
        check(!ed0->document()->isModified(), "file is clean after auto-save");
    }

    // --- commands must survive paths with spaces -----------------------
    const QString spacedDir = dir + "/a folder";
    QDir().mkpath(spacedDir);
    const QString spacedFile = spacedDir + "/space file.py";
    {
        QFile f(spacedFile);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("print(1)\n");
    }
    const QStringList runArgs =
        LanguageRegistry::expandArgs("python3 {file}", spacedFile);
    check(runArgs.size() == 2 && runArgs.at(1) == spacedFile,
          "spaces in a path stay inside one argument");
    const QStringList buildArgs =
        LanguageRegistry::expandArgs("g++ -g {file} -o {output}", spacedFile);
    check(buildArgs.contains(spacedFile),
          "compiler gets the spaced source path as one argument");
    check(buildArgs.contains(spacedDir + "/space file.bin"),
          "spaced output path stays one argument");
    const QStringList javaArgs =
        LanguageRegistry::expandArgs("java -cp {dir} {class}", spacedFile);
    check(javaArgs.size() == 4 && javaArgs.at(2) == spacedDir,
          "classpath with spaces stays one argument");

    // End-to-end: actually execute python on a script in a spaced directory.
    QProcess py;
    py.setWorkingDirectory(spacedDir);
    py.start(runArgs.first(), runArgs.mid(1));
    const bool ran = py.waitForFinished(5000);
    check(ran && py.exitCode() == 0,
          "python executes a file whose path contains spaces");

    // Don't trigger save prompts while closing tabs in this test.
    for (int i = 0; i < tabs->count(); ++i) {
        if (auto* e = qobject_cast<CodeEditor*>(tabs->widget(i)))
            e->document()->setModified(false);
    }

    // Close the active tab, then the remaining ones.
    closeTab(tabs, 1);
    check(tabs->count() == 2, "closing a tab removes it");
    check(tabs->currentIndex() >= 0, "another tab becomes active");

    closeTab(tabs, 0);
    closeTab(tabs, 0);
    check(tabs->count() == 0, "all tabs can be closed");
    check(stack->currentIndex() == 0, "closing the last tab returns home");

    if (failures) {
        fprintf(stderr, "\nFAILED (%d)\n", failures);
        return 1;
    }
    fprintf(stderr, "\nPASS\n");
    return 0;
}
