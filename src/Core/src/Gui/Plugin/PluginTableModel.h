#pragma once

#include <QAbstractTableModel>
#include <QPointer>
#include <Termaster/Core/Plugin/PluginManager.h>

/** Table projection of the live manager; no plugin state is copied or persisted. */
class PluginTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        Enabled,
        Name,
        Id,
        Version,
        Status,
        ColumnCount
    };
    enum Role
    {
        FilterRole = Qt::UserRole + 1024,
        SortRole
    };

private:
    QPointer<PluginManager> _manager;

public:
    explicit PluginTableModel(PluginManager& manager, QObject* parent = nullptr);

    int           rowCount(const QModelIndex& parent = {}) const override;
    int           columnCount(const QModelIndex& parent = {}) const override;
    QVariant      data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool          setData(const QModelIndex& index, const QVariant& value, int role) override;

    static QString statusText(PluginManager::Status status);

signals:
    void enabledChangeRequested(const QString& id, bool enabled);
};
