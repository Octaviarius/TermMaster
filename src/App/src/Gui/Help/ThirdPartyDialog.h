#pragma once

#include <QDialog>
#include <QList>

class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTextBrowser;
class QTreeView;

/** Offline presentation of the build's bundled component manifest and original notices. */
class ThirdPartyDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ThirdPartyDialog(QWidget* parent = nullptr);

private:
    struct Notice
    {
        QString title;
        QString path;
    };
    struct Component
    {
        QString       id;
        QString       name;
        QString       version;
        QString       authors;
        QString       homepage;
        QString       license;
        QString       icon;
        QList<Notice> notices;
    };
    enum Role
    {
        ComponentRole = Qt::UserRole + 1,
        SearchRole
    };

    QList<Component>       _components;
    QStandardItemModel*    _model    = nullptr;
    QSortFilterProxyModel* _filter   = nullptr;
    QTreeView*             _tree     = nullptr;
    QLabel*                _count    = nullptr;
    QTextBrowser*          _overview = nullptr;
    QComboBox*             _notices  = nullptr;
    QTextBrowser*          _license  = nullptr;
    int                    _current  = -1;

    bool _load(QString& error);
    void _select(const QModelIndex& index);
    void _showNotice(int index);
    void _filterChanged(const QString& text);
};
