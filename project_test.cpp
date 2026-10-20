#include "projectpanel.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QTimer>

#include <cstdio>

// filterAcceptsRow() is protected — expose it so the filtering rules can be
// checked directly, without relying on the tree view.
class ExposedProxy : public ProjectProxy {
public:
    using ProjectProxy::filterAcceptsRow;
};

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::fprintf(stderr, "%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

static bool writeFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write("int main() { return 0; }\n");
    return true;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Keep this test's settings away from the real editor settings.
    QCoreApplication::setApplicationName("tgk-editor-projecttest");
    QCoreApplication::setOrganizationName("tgk");

    const QString base = QDir::tempPath() + "/tgk_project_test";
    QDir(base).removeRecursively();
    if (!QDir().mkpath(base + "/src") || !QDir().mkpath(base + "/docs"))
        return 2;

    const QString mainPath = base + "/src/main.cpp";
    const QString utilPath = base + "/src/util.cpp";
    const QString readmePath = base + "/docs/readme.txt";
    const QString notesPath = base + "/notes.txt";
    if (!writeFile(mainPath) || !writeFile(utilPath) ||
        !writeFile(readmePath) || !writeFile(notesPath))
        return 2;

    QFileSystemModel model;
    model.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    model.setRootPath(base);

    auto waitForLoad = [&model](const QString& dir) {
        const QModelIndex idx = model.index(dir);
        if (model.canFetchMore(idx))
            model.fetchMore(idx);
        QEventLoop loop;
        QMetaObject::Connection c = QObject::connect(
            &model, &QFileSystemModel::directoryLoaded, &loop,
            [&loop, dir](const QString& loaded) {
                if (loaded == dir)
                    loop.quit();
            });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        if (model.rowCount(idx) == 0)
            loop.exec();
        QObject::disconnect(c);
    };

    waitForLoad(base);
    model.index(base + "/src");
    waitForLoad(base + "/src");
    model.index(base + "/docs");
    waitForLoad(base + "/docs");

    auto child = [&model](const QString& parentPath, const QString& name) {
        const QModelIndex parent = model.index(parentPath);
        for (int r = 0; r < model.rowCount(parent); ++r) {
            const QModelIndex idx = model.index(r, 0, parent);
            if (model.fileName(idx) == name)
                return idx;
        }
        return QModelIndex();
    };

    const QModelIndex root = model.index(base);
    const QModelIndex srcDir = child(base, "src");
    const QModelIndex docsDir = child(base, "docs");
    const QModelIndex notes = child(base, "notes.txt");
    const QModelIndex mainFile = child(base + "/src", "main.cpp");
    const QModelIndex utilFile = child(base + "/src", "util.cpp");

    check(srcDir.isValid() && docsDir.isValid() && notes.isValid() &&
              mainFile.isValid() && utilFile.isValid(),
          "the file system model read the test folder");
    if (!mainFile.isValid())
        return 1;

    ExposedProxy proxy;
    proxy.setSourceModel(&model);

    // --- checkboxes ---------------------------------------------------
    const QModelIndex mainProxy = proxy.mapFromSource(mainFile);
    check(mainProxy.isValid(), "a file maps into the project proxy");
    check(proxy.data(mainProxy, Qt::CheckStateRole).toInt() == Qt::Unchecked,
          "files start unchecked");
    check(proxy.flags(mainProxy).testFlag(Qt::ItemIsUserCheckable),
          "files can be checked");
    check(!proxy.flags(proxy.mapFromSource(srcDir))
               .testFlag(Qt::ItemIsUserCheckable),
          "folders have no checkbox");

    check(proxy.setData(mainProxy, Qt::Checked, Qt::CheckStateRole),
          "checking a file succeeds");
    check(proxy.pinned().contains(mainPath), "a checked file is pinned");
    check(proxy.data(mainProxy, Qt::CheckStateRole).toInt() == Qt::Checked,
          "the checked state is reported back");
    check(proxy.setData(mainProxy, Qt::Unchecked, Qt::CheckStateRole),
          "unchecking a file succeeds");
    check(proxy.pinned().isEmpty(), "an unchecked file is not pinned");

    // --- the "only pinned" filter -------------------------------------
    check(proxy.filterAcceptsRow(notes.row(), root),
          "filter off keeps every file visible");

    check(proxy.setData(proxy.mapFromSource(mainFile), Qt::Checked,
                        Qt::CheckStateRole),
          "the file can be pinned again");
    proxy.setOnlyPinned(true);
    check(proxy.filterAcceptsRow(mainFile.row(), model.index(base + "/src")),
          "a pinned file stays visible when filtering");
    check(!proxy.filterAcceptsRow(utilFile.row(), model.index(base + "/src")),
          "its unpinned sibling is hidden when filtering");
    check(proxy.filterAcceptsRow(srcDir.row(), root),
          "a folder containing a pinned file stays visible");
    check(!proxy.filterAcceptsRow(docsDir.row(), root),
          "a folder without pinned files is hidden");
    check(!proxy.filterAcceptsRow(notes.row(), root),
          "an unpinned top-level file is hidden");

    QSet<QString> pins;
    pins.insert(readmePath);
    proxy.setPinned(pins);
    check(proxy.filterAcceptsRow(docsDir.row(), root),
          "pinning a deep file keeps its folder visible");
    check(!proxy.filterAcceptsRow(mainFile.row(), model.index(base + "/src")),
          "replacing the pin set removes the old pin");

    // With no pins at all, filtering must not blank the tree.
    proxy.setPinned({});
    check(proxy.filterAcceptsRow(notes.row(), root),
          "the filter never hides everything when no file is pinned");

    proxy.setOnlyPinned(false);
    check(proxy.filterAcceptsRow(notes.row(), root),
          "turning the filter off shows the whole tree again");

    // --- panel wiring ---------------------------------------------------
    {
        ProjectPanel panel;
        panel.setRoot(base);
        panel.setPinnedFiles({mainPath, readmePath});
        QStringList expected{mainPath, readmePath};
        expected.sort();
        check(panel.pinnedFiles() == expected,
              "the panel remembers pinned files for its folder");
        panel.setOnlyPinned(true);
        check(panel.onlyPinned(), "the panel can enable the filter");
    }

    QDir(base).removeRecursively();

    std::fprintf(stderr, "%s\n", g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
