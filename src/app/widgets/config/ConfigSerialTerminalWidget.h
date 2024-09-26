#ifndef CONFIGSERIALWIDGET_H
#define CONFIGSERIALWIDGET_H

#include "managers/SettingsManager.h"
#include "terminal/SerialTerminal.h"
#include "widgets/config/IConfigWidget.h"

#include <QSerialPortInfo>
#include <QSet>

namespace Ui
{
class ConfigSerialTerminalWidget;
} // namespace Ui

class ConfigSerialTerminalWidget : public IConfigWidget
{
    Q_OBJECT

public:
    ConfigSerialTerminalWidget(QWidget* parent = nullptr);
    ~ConfigSerialTerminalWidget();

    void commit() override;

private slots:
    void updatePorts(QSet<QSerialPortInfo> availableSerialPorts,
                     QSet<QSerialPortInfo> toAdd,
                     QSet<QSerialPortInfo> toDel);

private:
    Ui::ConfigSerialTerminalWidget* ui;
    QMap<QString, QSerialPortInfo>  _portInfosMap;
    QMap<QString, QIcon>            _devIcons;

    UndoableSettings     _generalSettings;
    SerialTerminalConfig _serialConfig;

private slots:
    void _cmbNameChanged(int idx);
};

#endif // CONFIGSERIALWIDGET_H
