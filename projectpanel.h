#pragma once

#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QWidget>

#include "themeh.h"

class QCheckBox;
class QFileSystemModel;
class QLabel;
class QTreeView;

// Shows check boxes for files and can hide everything that is not pinned.
class ProjectProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ProjectProxy(QObject* parent = nullptr);

    void setPinned(const QSet<QString>& pinned);
    QSet<QString> pinned() const { return m_pinned; }
    void setOnlyPinned(bool only);
    bool onlyPinned() const { return m_onlyPinned; }

    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Absolute path of a proxy index (empty for invalid indexes).
    QString pathOf(const QModelIndex& index) const;

signals:
    void pinnedChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    bool hasPinnedUnder(const QString& dir) const;

    QSet<QString> m_pinned;
    bool m_onlyPinned = false;
};

// Left sidebar: open a folder, browse its files, open them in tabs and pin
// the ones that should stay visible when the filter is on.
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget* parent = nullptr);

    void setRoot(const QString& dir);
    QString root() const { return m_root; }

    void setOnlyPinned(bool only);
    bool onlyPinned() const;

    // The pinned-only toggle lives in the Explorer title bar; Ide reparents it.
    QCheckBox* pinnedOnlyToggle() const { return m_onlyPinned; }

    void applyTheme(const Theme& theme);
    void revealFile(const QString& path);

    QStringList pinnedFiles() const;
    void setPinnedFiles(const QStringList& paths);

    void refresh();

public slots:
    void openFolderDialog();

signals:
    void fileActivated(const QString& path);
    void rootChanged(const QString& dir);

private:
    void pinSelection(bool pin);
    void pinPath(const QString& path, bool pin);
    void updateRootLabel();
    void loadPins();
    void savePins();
    void updateTreeRoot();
    void tryReveal();

    QFileSystemModel* m_model = nullptr;
    ProjectProxy* m_proxy = nullptr;
    QTreeView* m_tree = nullptr;
    QLabel* m_rootLabel = nullptr;
    QCheckBox* m_onlyPinned = nullptr;
    QString m_root;
    QString m_pendingReveal;
};
