#ifndef ConfigTerminalEmulationWidget_H
#define ConfigTerminalEmulationWidget_H

#include "modules/Base/widgets/ConfigBaseWidget.h"

namespace Ui
{
class ConfigTerminalEmulationWidget;
} // namespace Ui

class ConfigTerminalEmulationWidget : public ConfigBaseWidget
{
    Q_OBJECT

public:
    explicit ConfigTerminalEmulationWidget(SettingsProxy config, QWidget* parent = nullptr);
    ~ConfigTerminalEmulationWidget();

    void commit() override;

protected:
    Ui::ConfigTerminalEmulationWidget* ui;
    QStringList                        _fontsList;
};

#endif // ConfigTerminalEmulationWidget_H
