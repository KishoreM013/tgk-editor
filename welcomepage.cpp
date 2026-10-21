#include "welcomepage.h"

#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(60, 40, 60, 30);

    outer->addStretch(1);

    m_title = new QLabel("tgk editor", this);
    QFont tf = m_title->font();
    tf.setPointSize(36);
    tf.setBold(true);
    m_title->setFont(tf);
    m_title->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_title);

    m_subtitle = new QLabel(
        "Multi-language IDE — C, C++, Java, Python and more", this);
    m_subtitle->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_subtitle);
    outer->addSpacing(22);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* newBtn = new QPushButton("＋  New File   (Ctrl+N)", this);
    auto* openBtn = new QPushButton("📂  Open File…   (Ctrl+O)", this);
    newBtn->setMinimumHeight(38);
    openBtn->setMinimumHeight(38);
    connect(newBtn, &QPushButton::clicked, this,
            &WelcomePage::newFileRequested);
    connect(openBtn, &QPushButton::clicked, this,
            &WelcomePage::openFileRequested);
    buttons->addWidget(newBtn);
    buttons->addSpacing(12);
    buttons->addWidget(openBtn);
    buttons->addStretch(1);
    outer->addLayout(buttons);

    m_back = new QPushButton("←  Back to open files", this);
    m_back->setFlat(true);
    m_back->hide();
    connect(m_back, &QPushButton::clicked, this,
            &WelcomePage::backToEditorRequested);
    outer->addWidget(m_back, 0, Qt::AlignHCenter);
    outer->addSpacing(18);

    auto* recentLabel = new QLabel("Recent files", this);
    QFont rf = recentLabel->font();
    rf.setBold(true);
    recentLabel->setFont(rf);
    outer->addWidget(recentLabel);

    m_emptyHint = new QLabel(
        "No recent files yet — open one with Ctrl+O and it will show up here.",
        this);
    outer->addWidget(m_emptyHint);

    m_recent = new QListWidget(this);
    m_recent->setMaximumHeight(320);
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
    outer->addWidget(m_recent);

    outer->addSpacing(10);
    auto* hint = new QLabel(
        "F9 compile & run  ·  Ctrl+\\ toggle comment  ·  "
        "Ctrl+Space completion (C/C++)",
        this);
    hint->setAlignment(Qt::AlignCenter);
    outer->addWidget(hint);

    outer->addStretch(2);
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

    const QString muted =
        t.mutedFg.isValid()
            ? t.mutedFg.name()
            : (t.editorBg.lightness() < 128 ? "#9aa0a6" : "#6a6a6a");
    m_subtitle->setStyleSheet("color: " + muted + ";");
    m_emptyHint->setStyleSheet("color: " + muted + ";");
}
