// Headless end-to-end test of the clangd LSP client:
// opens a real .cpp file in clangd and asks for completions at a known spot.
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <cstdio>

#include "clangdclient.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString dir = "/tmp/cppide_test";
    QDir().mkpath(dir);
    const QString file = dir + "/t.cpp";
    {
        QFile f(file);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#include <vector>\n"
                "int main() {\n"
                "    std::vector<int> v;\n"
                "    v.\n"
                "    return 0;\n"
                "}\n");
    }

    ClangdClient client;
    if (!client.start(dir)) {
        std::fprintf(stderr, "FAIL: could not start clangd\n");
        return 1;
    }
    std::fprintf(stderr, "DBG: clangd started\n");

    QFile f(file);
    f.open(QIODevice::ReadOnly | QIODevice::Text);
    QString text = QString::fromUtf8(f.readAll());
    client.openDocument(file, text);

    QEventLoop loop;
    QTimer::singleShot(12000, &loop, &QEventLoop::quit);
    QTimer::singleShot(1500, &loop, [&] {
        std::fprintf(stderr, "DBG: requesting completion\n");
        // line 3 (0-based), after "v." -> column 6
        client.completion(file, 3, 6, [&](bool ok, const QVector<ClangdItem>& items) {
            std::fprintf(stderr, "DBG: callback fired\n");
            std::printf("ok=%d items=%d\n", ok ? 1 : 0, int(items.size()));
            int shown = 0;
            for (const ClangdItem& it : items) {
                if (shown++ >= 100)
                    break;
                std::printf("  %-20s %s\n", qPrintable(it.label),
                            qPrintable(it.detail));
            }
            bool found = false;
            for (const ClangdItem& it : items)
                if (it.label.trimmed().startsWith("push_back"))
                    found = true;
            int pushCount = 0;
            for (const ClangdItem& it : items)
                if (it.label.contains("push")) {
                    ++pushCount;
                    std::fprintf(stderr, "DBG label=[%s]\n",
                                 qPrintable(it.label));
                }
            std::fprintf(stderr, "DBG pushCount=%d items=%d\n", pushCount,
                         int(items.size()));
            std::printf("%s\n", found ? "PASS: found push_back (IntelliSense works!)"
                                      : "FAIL: push_back not found");
            loop.quit();
        });
    });

    QObject::connect(&client, &ClangdClient::serverDied, &loop, [&] {
        std::fprintf(stderr, "DBG: server died\n");
        loop.quit();
    });
    loop.exec();
    return 0;
}
