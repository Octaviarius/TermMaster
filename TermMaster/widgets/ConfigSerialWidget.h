#ifndef CONFIGSERIALWIDGET_H
#define CONFIGSERIALWIDGET_H

#include "ConfigBaseWidget.h"
#include "core/Registree.h"

#include <QSerialPortInfo>

namespace Ui
{
class ConfigSerialWidget;
} // namespace Ui

class ConfigSerialWidget : public ConfigBaseWidget
{
    Q_OBJECT

public:
    explicit ConfigSerialWidget(Registree config, QWidget* parent = nullptr);
    ~ConfigSerialWidget();

    void commit() override;

public slots:
    void updatePorts();

private:
    Ui::ConfigSerialWidget* ui;
    QList<QSerialPortInfo>  _portInfo;

private slots:
    void _cmbNameChanged(int idx);
};

#endif // CONFIGSERIALWIDGET_H
