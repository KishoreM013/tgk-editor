// Headless checks for the multi-tab document handling.
// Run with: QT_QPA_PLATFORM=offscreen ./build/tabs_test

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextDocument>

#include <cstdio>

#include "ide.h"

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
    auto* editor = qobject_cast<QPlainTextEdit*>(tabs->widget(0));
    check(editor != nullptr, "tab page is an editor");
    editor->document()->setModified(true);
    check(tabs->tabText(0).contains("*"), "modified tab shows a marker");
    editor->document()->setModified(false);

    // Ctrl+Tab equivalent: tabs can be switched programmatically.
    tabs->setCurrentIndex(1);
    check(tabs->currentIndex() == 1, "tabs can be switched");

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
