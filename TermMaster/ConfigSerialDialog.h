#ifndef CONFIGSERIALDIALOG_H
#define CONFIGSERIALDIALOG_H

#include "utils.h"
#include <QDialog>
#include <QSerialPortInfo>

namespace Ui
{
class ConfigSerialDialog;
}

class ConfigSerialDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigSerialDialog(QWidget* parent = nullptr, const PathedValue& defaultConfig = PathedValue());
    ~ConfigSerialDialog();

    int show(PathedValue& config);

private:
    Ui::ConfigSerialDialog* ui;

    PathedValue _config;
    QList<QSerialPortInfo> _portInfo;

private slots:
    void _cmbNameChanged(int idx);
};

#endif // CONFIGSERIALDIALOG_H
