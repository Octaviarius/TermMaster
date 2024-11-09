#ifndef CONFIGSERIALWIDGET_H
#define CONFIGSERIALWIDGET_H

#include "managers/SettingsManager.h"
#include "terminal/SerialTerminal.h"
#include "widgets/config/IConfigWidget.h"

#include <QSerialPortInfo>
#include <QSet>

namespace Ui
{
class ConfigSerialTermWidget;
} // namespace Ui

class ConfigSerialTermWidget : public IConfigWidget
{
    Q_OBJECT

public:
    ConfigSerialTermWidget(QWidget* parent = nullptr);
    ~ConfigSerialTermWidget();

    void commit() override;

private slots:
    void updatePorts(QSet<QSerialPortInfo> availableSerialPorts,
                     QSet<QSerialPortInfo> toAdd,
                     QSet<QSerialPortInfo> toDel);

private:
    Ui::ConfigSerialTermWidget* ui;
    QMap<QString, QSerialPortInfo>  _portInfosMap;
    QMap<QString, QIcon>            _devIcons;

    Settings*            _generalSettings;
    SerialTerminalConfig _serialConfig;

private slots:
    void _cmbNameChanged(int idx);
};

#endif // CONFIGSERIALWIDGET_H
