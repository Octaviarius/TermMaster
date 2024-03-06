#ifndef CONFIGTERMINALDIALOG_H
#define CONFIGTERMINALDIALOG_H

#include "utils.h"
#include <QDialog>

namespace Ui
{
class ConfigTerminalDialog;
}

class ConfigTerminalDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigTerminalDialog(QWidget* parent = nullptr, const PathedValue& config = PathedValue());
    ~ConfigTerminalDialog();

    PathedValue config()
    {
        return _config;
    }

    int show(PathedValue& config);

private:
    Ui::ConfigTerminalDialog* ui;
    PathedValue _config;
    QStringList _fontsList;

private slots:

    void _onFontSelectClicked();
};

#endif // CONFIGTERMINALDIALOG_H
