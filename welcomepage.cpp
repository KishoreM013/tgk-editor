#include "welcomepage.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 20, 28, 20);
    contentLayout->setSpacing(10);

    m_subtitle = new QLabel(
        "Choose one of the options below\nto start working", content);
    m_subtitle->setObjectName("welcomeMuted");
    m_subtitle->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(m_subtitle);
    contentLayout->addSpacing(10);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);
    auto* openProject = new QPushButton("Open Project", content);
    openProject->setObjectName("welcomePrimary");
    openProject->setMinimumHeight(34);
    openProject->setToolTip("Open a project folder (Ctrl+Shift+O)");
    connect(openProject, &QPushButton::clicked, this,
            &WelcomePage::openProjectRequested);
    actions->addWidget(openProject);

    auto* openFile = new QPushButton("Open File", content);
    openFile->setMinimumHeight(34);
    openFile->setToolTip("Open a file (Ctrl+O)");
    connect(openFile, &QPushButton::clicked, this,
            &WelcomePage::openFileRequested);
    actions->addWidget(openFile);

    auto* newFile = new QPushButton("New File", content);
    newFile->setMinimumHeight(34);
    newFile->setToolTip("Create a new file (Ctrl+N)");
    connect(newFile, &QPushButton::clicked, this,
            &WelcomePage::newFileRequested);
    actions->addWidget(newFile);
    contentLayout->addLayout(actions);

    m_back = new QPushButton("Back to open files", content);
    m_back->setFlat(true);
    m_back->hide();
    connect(m_back, &QPushButton::clicked, this,
            &WelcomePage::backToEditorRequested);
    contentLayout->addWidget(m_back, 0, Qt::AlignCenter);
    contentLayout->addSpacing(10);
    auto* recentLabel = new QLabel("Recent files", content);
    recentLabel->setObjectName("welcomeSection");
    contentLayout->addWidget(recentLabel);

    m_emptyHint = new QLabel(
        "Your recently opened files will appear here.", content);
    m_emptyHint->setObjectName("welcomeMuted");
    contentLayout->addWidget(m_emptyHint);

    m_recent = new QListWidget(content);
    m_recent->setMinimumHeight(180);
    m_recent->setAlternatingRowColors(true);
    m_recent->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_recent, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) {
                if (item)
                    emit fileActivated(item->data(Qt::UserRole).toString());
            });
    connect(m_recent, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item)
                    emit fileActivated(item->data(Qt::UserRole).toString());
            });
    contentLayout->addWidget(m_recent, 1);
    outer->addWidget(content, 1);
}

void WelcomePage::setBackVisible(bool visible) {
    if (m_back)
        m_back->setVisible(visible);
}

void WelcomePage::setRecentFiles(const QStringList& files) {
    m_recent->clear();
    for (const QString& path : files) {
        QFileInfo fi(path);
        QString label = fi.fileName() + "    —    " + fi.absolutePath();
        auto* item = new QListWidgetItem(label, m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
    const bool empty = files.isEmpty();
    m_emptyHint->setVisible(empty);
    m_recent->setVisible(!empty);
}

void WelcomePage::applyTheme(const Theme& t) {
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, t.editorBg);
    p.setColor(QPalette::WindowText, t.editorFg);
    p.setColor(QPalette::Base, t.consoleBg);
    p.setColor(QPalette::Text, t.consoleFg);
    p.setColor(QPalette::Highlight, t.selectionBg);
    p.setColor(QPalette::HighlightedText, t.editorFg);
    setPalette(p);

    m_recent->setPalette(p);

    const QString panel = t.panelBg.isValid() ? t.panelBg.name() : t.consoleBg.name();
    const QString border = t.border.isValid() ? t.border.name() : "#3b3f46";
    const QString accent = t.accent.isValid() ? t.accent.name() : "#74ade8";
    const QString muted =
        t.mutedFg.isValid()
            ? t.mutedFg.name()
            : (t.editorBg.lightness() < 128 ? "#9aa0a6" : "#6a6a6a");
    setStyleSheet(QString(
                      "#welcomeSection { color: %3; font-weight: 600; }"
                      "#welcomeMuted { color: %4; }"
                      "QPushButton { "
                      "background: transparent; color: %3; border: 1px solid "
                      "transparent; border-radius: 4px; padding: 6px; }"
                      "QPushButton:hover "
                      "{ background: %2; }"
                      "#welcomePrimary { border-color: %5; color: %5; }"
                      "#welcomePrimary:hover { background: %5; color: %1; }"
                      "QListWidget { background: %6; border: 1px solid %2; "
                      "color: %3; }")
                      .arg(panel, border, t.editorFg.name(), muted, accent,
                           t.editorBg.name()));
}
