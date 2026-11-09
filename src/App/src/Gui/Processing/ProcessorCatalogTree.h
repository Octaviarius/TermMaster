#pragma once

#include <QPointer>
#include <QTreeWidget>

class ProcessorFavorites;

/** Common catalog view: native Qt hierarchy, shared favorites and dashed divider. */
class ProcessorCatalogTree : public QTreeWidget
{
    Q_OBJECT
public:
    enum Role
    {
        FavoriteRole = Qt::UserRole + 30,
        SeparatorRole
    };
    explicit ProcessorCatalogTree(QWidget* parent = nullptr);
    void          setFavorites(ProcessorFavorites* favorites);
    void          prependFavorites(const QString& query = {}, const QStringList& excluded = {});
    Q_SIGNAL void favoritesChanged();

private:
    QPointer<ProcessorFavorites> _favorites;
    void                         _menu(const QPoint& position);
};
