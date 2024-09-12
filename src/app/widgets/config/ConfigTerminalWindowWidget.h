#ifndef ConfigTerminalWindowWidget_H
#define ConfigTerminalWindowWidget_H

#include "widgets/Config/ConfigBaseWidget.h"

namespace Ui
{
class ConfigTerminalWindowWidget;
} // namespace Ui

class ConfigTerminalWindowWidget : public ConfigBaseWidget
{
    Q_OBJECT

public:
    explicit ConfigTerminalWindowWidget(SettingsProxy config, QWidget* parent = nullptr);
    ~ConfigTerminalWindowWidget();

    void commit() override;

protected:
    Ui::ConfigTerminalWindowWidget* ui;
    QStringList                     _fontsList;

private slots:

    void _onFontSelectClicked();
};

#endif // ConfigTerminalWindowWidget_H
