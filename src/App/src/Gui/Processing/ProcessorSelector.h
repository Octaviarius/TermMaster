#pragma once

#include <QComboBox>
#include <Termaster/Core/Processing/ProcessorCatalog.h>

class QFrame;
class ProcessorCatalogTree;
class ProcessorFavorites;
class QTreeWidgetItem;
class QLineEdit;

/** Compact processor selection with a searchable, grouped catalog popup. */
class ProcessorSelector final : public QComboBox
{
    Q_OBJECT
public:
    explicit ProcessorSelector(QWidget* parent = nullptr, ProcessorFavorites* favorites = nullptr);
    void setCatalog(QList<ProcessorCatalog::Type> types, const QString& selectedId, QStringList excludedFavorites = {});
    void setTopology(int channels);
    void setInplaceAvailable(bool available);
    void showPopup() override;
    void hidePopup() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Role
    {
        SearchRole = Qt::UserRole + 2,
        ExpandedRole
    };
    QList<ProcessorCatalog::Type> _types;
    QStringList                   _excludedFavorites;
    QFrame*                       _popup;
    ProcessorCatalogTree*         _tree;
    QLineEdit*                    _search;
    int                           _channels         = 1;
    bool                          _inplaceAvailable = false;
    bool                          _filtering        = false;

    bool _supports(const ProcessorCatalog::Type& type) const;
    void _refreshSelection(const QString& selectedId);

    void _rebuild();
    void _filter();
    void _accept(QTreeWidgetItem* item);
};
