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
    void setCatalog(QList<ProcessorCatalog::Type> types, const QString& selectedId);
    void showPopup() override;
    void hidePopup() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QList<ProcessorCatalog::Type> _types;
    QFrame*                       _popup;
    ProcessorCatalogTree*         _tree;
    QLineEdit*                    _search;

    void _rebuild();
    void _accept(QTreeWidgetItem* item);
};
