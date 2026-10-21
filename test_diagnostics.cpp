// Headless end-to-end test of live clangd diagnostics:
// open a valid file, break it, fix it, and make sure the squiggles follow.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "clangdclient.h"

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

// Run the event loop for a while (clangd answers asynchronously).
static void spin(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

static void writeFile(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(text.toUtf8());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString dir = "/tmp/cppide_diagnostics_test";
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString file = dir + "/t.cpp";

    const QString good =
        "#include <iostream>\n"
        "#include <string>\n"
        "using namespace std;\n"
        "int main() {\n"
        "    int num = 0;\n"
        "    string s;\n"
        "    cout << num << s << endl;\n"
        "    return 0;\n"
        "}\n";
    const QString bad =
        "#include <iostream>\n"
        "using namespace std;\n"
        "int main() {\n"
        "    string s\n"
        "}\n";

    writeFile(file, good);
    writeFile(dir + "/compile_commands.json",
              QString("[{\"directory\":\"%1\",\"file\":\"%2\",\"arguments\":"
                      "[\"g++\",\"-std=c++17\",\"-Wall\",\"-Wextra\",\"%2\"]}]\n")
                  .arg(dir, file));

    ClangdClient client;
    QObject::connect(&client, &ClangdClient::serverDied, [&] {
        std::printf("SERVER DIED\n");
    });
    bool received = false;
    QVector<ClangdDiagnostic> last;
    QObject::connect(&client, &ClangdClient::diagnostics,
                     [&](const QString& path, const QVector<ClangdDiagnostic>& ds) {
                         if (path != file)
                             return;
                         received = true;
                         last = ds;
                         std::printf("DIAG %d:", int(ds.size()));
                         for (const ClangdDiagnostic& d : ds)
                             std::printf(" [%d:%d] %s", d.line, d.col,
                                         qPrintable(d.message));
                         std::printf("\n");
                     });

    if (!client.start(dir, dir)) {
        std::printf("FAIL: could not start clangd\n");
        return 1;
    }

    client.openDocument(file, good);
    spin(2500);
    check(received, "clangd publishes diagnostics for the open document");
    check(last.isEmpty(), "valid code has no diagnostics");

    received = false;
    client.changeDocument(file, bad);
    spin(2500);
    check(received && !last.isEmpty(), "invalid code produces diagnostics");

    received = false;
    client.changeDocument(file, good);
    spin(2500);
    check(received && last.isEmpty(), "fixing the code clears diagnostics");

    // Back-to-back replacements, like a fast paste followed by a correction.
    // The final publish must describe the newest text, never the older one.
    received = false;
    client.changeDocument(file, bad);
    spin(200);
    client.changeDocument(file, good);
    spin(3000);
    check(received && last.isEmpty(),
          "back-to-back replacements settle on the newest text");

    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
