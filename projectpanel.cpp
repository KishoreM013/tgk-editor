#include "projectpanel.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

// ------------------------------------------------------------------- proxy

ProjectProxy::ProjectProxy(QObject* parent) : QSortFilterProxyModel(parent) {}

void ProjectProxy::setPinned(const QSet<QString>& pinned) {
    m_pinned = pinned;
    emit pinnedChanged();
    if (m_onlyPinned)
        invalidateFilter();
}

void ProjectProxy::setOnlyPinned(bool only) {
    if (m_onlyPinned == only)
        return;
    m_onlyPinned = only;
    invalidateFilter();
}

QString ProjectProxy::pathOf(const QModelIndex& index) const {
    auto* fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs || !index.isValid())
        return {};
    return fs->filePath(mapToSource(index));
}

bool ProjectProxy::hasPinnedUnder(const QString& dir) const {
    const QString prefix = dir.endsWith('/') ? dir : dir + '/';
    for (const QString& p : m_pinned) {
        if (p.startsWith(prefix))
            return true;
    }
    return false;
}

QVariant ProjectProxy::data(const QModelIndex& index, int role) const {
    if (role == Qt::CheckStateRole && index.column() == 0) {
        auto* fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fs)
            return {};
        const QModelIndex src = mapToSource(index);
        if (!src.isValid() || fs->isDir(src))
            return {}; // folders don't get a check box
        return m_pinned.contains(fs->filePath(src)) ? Qt::Checked
                                                    : Qt::Unchecked;
    }
    return QSortFilterProxyModel::data(index, role);
}

Qt::ItemFlags ProjectProxy::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QSortFilterProxyModel::flags(index);
    if (index.column() == 0) {
        auto* fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (fs && !fs->isDir(mapToSource(index)))
            f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

bool ProjectProxy::setData(const QModelIndex& index, const QVariant& value,
                           int role) {
    if (role == Qt::CheckStateRole && index.column() == 0) {
        const QString path = pathOf(index);
        if (path.isEmpty())
            return false;
        const bool on = value.toInt() == Qt::Checked;
        if (on)
            m_pinned.insert(path);
        else
            m_pinned.remove(path);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        emit pinnedChanged();
        if (m_onlyPinned)
            invalidateFilter();
        return true;
    }
    return QSortFilterProxyModel::setData(index, value, role);
}

bool ProjectProxy::filterAcceptsRow(int row, const QModelIndex& parent) const {
    if (!m_onlyPinned || m_pinned.isEmpty())
        return true; // nothing pinned yet: don't hide the whole tree
    auto* fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs)
        return true;
    const QModelIndex idx = fs->index(row, 0, parent);
    if (!idx.isValid())
        return true;
    if (fs->isDir(idx))
        return hasPinnedUnder(fs->filePath(idx));
    return m_pinned.contains(fs->filePath(idx));
}

// ------------------------------------------------------------------- panel

ProjectPanel::ProjectPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    auto* top = new QHBoxLayout();
    auto* openBtn = new QPushButton("Open Folder…", this);
    openBtn->setToolTip("Choose a folder to show in the tree (Ctrl+Shift+O)");
    auto* refreshBtn = new QPushButton("⟳", this);
    refreshBtn->setToolTip("Refresh the tree");
    refreshBtn->setFixedWidth(30);
    top->addWidget(openBtn, 1);
    top->addWidget(refreshBtn);
    outer->addLayout(top);

    m_rootLabel = new QLabel("No folder open", this);
    m_rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont bold = m_rootLabel->font();
    bold.setBold(true);
    m_rootLabel->setFont(bold);
    outer->addWidget(m_rootLabel);

    auto* filterRow = new QHBoxLayout();
    m_onlyPinned = new QCheckBox("Only pinned files", this);
    m_onlyPinned->setToolTip(
        "Show only files whose check box is ticked.\n"
        "Tick files in the tree first (Ctrl+click to select several).");
    auto* pinBtn = new QPushButton("Pin", this);
    pinBtn->setToolTip("Tick all selected files (Ctrl+click to select several)");
    pinBtn->setFixedWidth(44);
    filterRow->addWidget(m_onlyPinned);
    filterRow->addStretch(1);
    filterRow->addWidget(pinBtn);
    outer->addLayout(filterRow);

    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_model->setResolveSymlinks(true);

    m_proxy = new ProjectProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(true);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < 4; ++c)
        m_tree->hideColumn(c);
    m_tree->setAnimated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    outer->addWidget(m_tree, 1);

    connect(openBtn, &QPushButton::clicked, this,
            &ProjectPanel::openFolderDialog);
    connect(refreshBtn, &QPushButton::clicked, this, &ProjectPanel::refresh);
    connect(pinBtn, &QPushButton::clicked, this,
            [this] { pinSelection(true); });
    connect(m_onlyPinned, &QCheckBox::toggled, this, [this](bool on) {
        m_proxy->setOnlyPinned(on);
        QSettings s;
        s.setValue("projectOnlyPinned", on);
        s.sync();
        if (on)
            m_tree->expandToDepth(1);
    });

    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex& i) {
        if (!i.isValid())
            return;
        const QString path = m_proxy->pathOf(i);
        auto* fs = qobject_cast<QFileSystemModel*>(m_proxy->sourceModel());
        if (!path.isEmpty() && fs && !fs->isDir(m_proxy->mapToSource(i)))
            emit fileActivated(path);
    });

    connect(m_tree, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QModelIndex idx = m_tree->indexAt(pos);
                QMenu menu(this);
                if (idx.isValid()) {
                    const QString path = m_proxy->pathOf(idx);
                    auto* fs =
                        qobject_cast<QFileSystemModel*>(m_proxy->sourceModel());
                    if (fs && !path.isEmpty() &&
                        !fs->isDir(m_proxy->mapToSource(idx))) {
                        menu.addAction("Open", this, [this, path] {
                            emit fileActivated(path);
                        });
                        const bool pinned = m_proxy->pinned().contains(path);
                        menu.addAction(pinned ? "Unpin (hide when filtered)"
                                              : "Pin (keep when filtered)",
                                       this, [this, path, pinned] {
                                           pinPath(path, !pinned);
                                       });
                        menu.addSeparator();
                    }
                }
                menu.addAction("Pin selected files", this,
                               [this] { pinSelection(true); });
                menu.addAction("Unpin selected files", this,
                               [this] { pinSelection(false); });
                menu.addSeparator();
                menu.addAction("Expand all", m_tree, &QTreeView::expandAll);
                menu.addAction("Collapse all", m_tree, &QTreeView::collapseAll);
                menu.exec(m_tree->viewport()->mapToGlobal(pos));
            });

    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this](const QString& path) {
                if (path == m_root)
                    updateTreeRoot();
                if (!m_pendingReveal.isEmpty() &&
                    m_pendingReveal.startsWith(path + "/"))
                    tryReveal();
            });

    // restore saved filter state (without re-saving during startup)
    const bool savedFilter = QSettings().value("projectOnlyPinned", false).toBool();
    m_onlyPinned->setChecked(savedFilter);
    m_proxy->setOnlyPinned(savedFilter);
}

void ProjectPanel::openFolderDialog() {
    const QString start = m_root.isEmpty() ? QDir::homePath() : m_root;
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Open folder", start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty())
        setRoot(dir);
}

void ProjectPanel::setRoot(const QString& dir) {
    const QFileInfo fi(dir);
    if (!fi.isDir())
        return;
    m_root = fi.absoluteFilePath();
    m_model->setRootPath(m_root);
    loadPins();
    updateTreeRoot();
    updateRootLabel();
    emit rootChanged(m_root);
}

void ProjectPanel::updateTreeRoot() {
    if (m_root.isEmpty())
        return;
    const QModelIndex src = m_model->index(m_root);
    m_tree->setRootIndex(m_proxy->mapFromSource(src));
}

void ProjectPanel::refresh() {
    if (m_root.isEmpty())
        return;
    // Force the file system model to re-scan this folder.
    m_model->setRootPath(QString());
    m_model->setRootPath(m_root);
    updateTreeRoot();
    m_tree->viewport()->update();
}

void ProjectPanel::setOnlyPinned(bool only) {
    m_onlyPinned->setChecked(only);
    m_proxy->setOnlyPinned(only);
}

bool ProjectPanel::onlyPinned() const { return m_onlyPinned->isChecked(); }

void ProjectPanel::updateRootLabel() {
    if (m_root.isEmpty()) {
        m_rootLabel->setText("No folder open");
        m_rootLabel->setToolTip(QString());
        return;
    }
    m_rootLabel->setText(QFileInfo(m_root).fileName());
    m_rootLabel->setToolTip(QDir::toNativeSeparators(m_root));
}

QStringList ProjectPanel::pinnedFiles() const {
    QStringList list;
    for (const QString& p : m_proxy->pinned())
        list << p;
    list.sort();
    return list;
}

void ProjectPanel::setPinnedFiles(const QStringList& paths) {
    QSet<QString> pinned;
    for (const QString& p : paths)
        pinned.insert(p);
    m_proxy->setPinned(pinned);
    savePins();
    m_tree->viewport()->update();
}

void ProjectPanel::pinPath(const QString& path, bool pin) {
    QSet<QString> pinned = m_proxy->pinned();
    if (pin)
        pinned.insert(path);
    else
        pinned.remove(path);
    m_proxy->setPinned(pinned);
    savePins();
    m_tree->viewport()->update();
}

void ProjectPanel::pinSelection(bool pin) {
    const QModelIndexList rows = m_tree->selectionModel()->selectedRows(0);
    if (rows.isEmpty())
        return;
    auto* fs = qobject_cast<QFileSystemModel*>(m_proxy->sourceModel());
    if (!fs)
        return;

    QSet<QString> pinned = m_proxy->pinned();
    for (const QModelIndex& proxyIdx : rows) {
        const QModelIndex src = m_proxy->mapToSource(proxyIdx);
        if (!src.isValid() || fs->isDir(src))
            continue;
        const QString path = fs->filePath(src);
        if (pin)
            pinned.insert(path);
        else
            pinned.remove(path);
    }
    m_proxy->setPinned(pinned);
    savePins();
    m_tree->viewport()->update();
}

void ProjectPanel::revealFile(const QString& path) {
    if (m_root.isEmpty() || path.isEmpty())
        return;
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (!abs.startsWith(m_root + "/"))
        return;
    m_pendingReveal = abs;
    tryReveal();
}

void ProjectPanel::tryReveal() {
    if (m_pendingReveal.isEmpty())
        return;
    const QModelIndex src = m_model->index(m_pendingReveal);
    if (!src.isValid())
        return; // directory not loaded yet; directoryLoaded() retries
    const QModelIndex idx = m_proxy->mapFromSource(src);
    if (!idx.isValid())
        return; // hidden by the "only pinned" filter
    m_pendingReveal.clear();
    for (QModelIndex parent = idx.parent(); parent.isValid();
         parent = parent.parent())
        m_tree->expand(parent);
    m_tree->scrollTo(idx);
    m_tree->setCurrentIndex(idx);
}

void ProjectPanel::applyTheme(const Theme& t) {
    QPalette p = m_tree->palette();
    p.setColor(QPalette::Base, t.editorBg);
    p.setColor(QPalette::Text, t.editorFg);
    p.setColor(QPalette::Highlight, t.selectionBg);
    p.setColor(QPalette::HighlightedText, t.editorFg);
    m_tree->setPalette(p);
    m_tree->viewport()->update();
}

// Pins are stored per project folder in the usual settings file.
static QString pinsKey(const QString& root) {
    return QString::fromLatin1(QUrl::toPercentEncoding(root));
}

void ProjectPanel::loadPins() {
    QSettings s;
    s.beginGroup("pinnedFiles");
    QSet<QString> pinned;
    for (const QString& p : s.value(pinsKey(m_root)).toStringList())
        pinned.insert(p);
    s.endGroup();
    m_proxy->setPinned(pinned);
}

void ProjectPanel::savePins() {
    QSettings s;
    s.beginGroup("pinnedFiles");
    QStringList list;
    for (const QString& p : m_proxy->pinned())
        list << p;
    list.sort();
    s.setValue(pinsKey(m_root), list);
    s.endGroup();
    s.sync();
}
