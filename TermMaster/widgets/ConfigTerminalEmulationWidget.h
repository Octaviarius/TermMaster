#ifndef ConfigTerminalEmulationWidget_H
#define ConfigTerminalEmulationWidget_H

#include "ConfigBaseWidget.h"
#include "Registree.h"

namespace Ui
{
class ConfigTerminalEmulationWidget;
} // namespace Ui

class ConfigTerminalEmulationWidget : public ConfigBaseWidget
{
    Q_OBJECT

public:
    explicit ConfigTerminalEmulationWidget(Registree config, QWidget* parent = nullptr);
    ~ConfigTerminalEmulationWidget();

    void commit() override;

protected:
    Ui::ConfigTerminalEmulationWidget* ui;
    QStringList                        _fontsList;
};

#endif // ConfigTerminalEmulationWidget_H
