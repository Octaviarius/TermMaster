#pragma once
#include "../../PortClassification.h"

#include <QAbstractListModel>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <Termaster/Core/plugin/PluginHost.h>
class QComboBox;
class QLabel;

class PortModel final : public QAbstractListModel
{
public:
    struct Port
    {
        QString                    name;
        QString                    location;
        QString                    description;
        QString                    manufacturer;
        QString                    serial;
        QString                    identifiers;
        PortClassification::Result classification;
        bool                       operator==(const Port& other) const;
    };
    explicit PortModel(QObject* parent = nullptr);
    int                rowCount(const QModelIndex& parent = {}) const override;
    QVariant           data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void               setPorts(QList<Port> ports, const QString& configured);
    const Port*        port(int row) const;
    static QList<Port> availablePorts();

private:
    QList<Port> _ports;
};

/** Widget-owned polling; injected inventories never enumerate hardware. */
class PortSelector final : public QWidget, public PluginHost::FieldEditor
{
public:
    using Inventory = std::function<QList<PortModel::Port>()>;
    explicit PortSelector(const QVariantMap& options, QWidget* parent = nullptr, Inventory inventory = {});
    ~PortSelector() override;
    QWidget* widget() override;
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    void     setChangedCallback(std::function<void()> callback) override;
    void     refresh();

private:
    PortModel*             _model;
    QComboBox*             _combo;
    QLabel*                _details;
    QTimer                 _timer;
    Inventory              _inventory;
    QThread*               _worker = nullptr;
    QList<PortModel::Port> _snapshot;

    std::function<void()> _changed;
    void                  _applySnapshot();
    void                  _updateDetails();
};
