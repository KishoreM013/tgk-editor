// End-to-end GUI test: does the editor's squiggles follow the text?
// Opens a broken C++ file, lets the IDE start clangd, then fixes the file and
// expects the error underlines to disappear.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QThread>
#include <cstdio>
#include <functional>

#include "codeeditor.h"
#include "ide.h"

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static bool waitUntil(const std::function<bool()>& done, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(10);
    }
    return done();
}

static int countWaves(CodeEditor* editor) {
    int waves = 0;
    for (const auto& selection : editor->extraSelections())
        if (selection.format.underlineStyle() == QTextCharFormat::WaveUnderline)
            ++waves;
    return waves;
}

static bool writeFile(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(text.toUtf8()) == text.toUtf8().size();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    const QString dir = "/tmp/cppide_ide_diagnostics_test";
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString file = dir + "/main.cpp";

    const QString broken =
        "#include <iostream>\n"
        "using namespace std;\n"
        "int main() {\n"
        "    string s\n"
        "}\n";
    const QString fixed =
        "#include <iostream>\n"
        "#include <string>\n"
        "using namespace std;\n"
        "int main() {\n"
        "    string s = \"ok\";\n"
        "    cout << s << endl;\n"
        "    return 0;\n"
        "}\n";

    if (!writeFile(file, broken)) {
        std::printf("FAIL: could not create test file\n");
        return 1;
    }

    Ide window;
    window.show();
    window.openFileFromPath(file);

    auto* tabs = window.findChild<QTabWidget*>();
    auto* editor = tabs ? qobject_cast<CodeEditor*>(tabs->currentWidget())
                        : nullptr;
    check(editor != nullptr, "the file opened in an editor");
    if (!editor)
        return 1;

    // This is what typing does; it starts clangd and opens the document.
    QMetaObject::invokeMethod(&window, "onCompletionRequested",
                              Qt::DirectConnection);

    const bool sawErrors = waitUntil([&] { return countWaves(editor) > 0; },
                                     15000);
    check(sawErrors, "clangd errors appear in the editor");

    // Fix the file without needing a save or another completion request.
    editor->setPlainText(fixed);
    const bool cleared = waitUntil([&] { return countWaves(editor) == 0; },
                                   15000);
    check(cleared, "fixing the text clears the clangd errors");

    // A file that only has includes has an unused-include hint from clangd.
    // It should appear in Problems as a hint, but not as a warning squiggle.
    auto* problems = window.findChild<QTableWidget*>();
    editor->setPlainText("#include <iostream>\nusing namespace std;\n");
    const bool hintArrived = waitUntil(
        [&] {
            if (!problems)
                return false;
            for (int row = 0; row < problems->rowCount(); ++row) {
                auto* severity = problems->item(row, 0);
                auto* message = problems->item(row, 1);
                if (severity && message && severity->text() == "hint" &&
                    message->text().contains("not used"))
                    return true;
            }
            return false;
        },
        15000);
    check(hintArrived, "unused include is reported as a hint");
    check(countWaves(editor) == 0,
          "unused include hint does not draw a warning squiggle");

    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
