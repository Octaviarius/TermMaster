#ifndef CONFIGBASEWIDGET_H
#define CONFIGBASEWIDGET_H

#include "core/SettingsManager.h"

#include <QWidget>

class ConfigBaseWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ConfigBaseWidget(SettingsProxy config, QWidget* parent = nullptr);

    template <typename T>
    static ConfigBaseWidget* constructWidget(SettingsProxy config, QWidget* parent = nullptr)
    {
        return new T(config, parent);
    }

    const SettingsProxy& config();
    virtual void     commit() = 0;

protected:
    SettingsProxy _config;
signals:
};

#endif // CONFIGBASEWIDGET_H
