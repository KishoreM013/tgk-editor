#pragma once

#include <QWidget>
#include <QStringList>

#include "themeh.h"

class QLabel;
class QListWidget;
class QPushButton;

// Start page: big title, New/Open buttons and the recent-files list.
class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);

    void setRecentFiles(const QStringList& files);
    void setBackVisible(bool visible);
    void applyTheme(const Theme& theme);

signals:
    void newFileRequested();
    void openFileRequested();
    void fileActivated(const QString& path);
    void backToEditorRequested();

private:
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_emptyHint = nullptr;
    QListWidget* m_recent = nullptr;
    QPushButton* m_back = nullptr;
};
