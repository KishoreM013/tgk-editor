#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFile>

#include "ide.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("tgkeditor");
    app.setOrganizationName("tgk");

    QString iconPath = QDir::homePath() +
                       "/.local/share/icons/hicolor/256x256/apps/tgkeditor.png";
    if (QFile::exists(iconPath))
        app.setWindowIcon(QIcon(iconPath));

    Ide w;
    w.show();

    QStringList args = QCoreApplication::arguments();
    args.removeFirst(); // program name
    for (const QString& a : args) {
        QFileInfo fi(a);
        if (fi.isFile()) {
            w.openFileFromPath(fi.absoluteFilePath());
            break;
        }
    }
    return app.exec();
}
