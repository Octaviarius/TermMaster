#ifndef CONFIGBASEWIDGET_H
#define CONFIGBASEWIDGET_H

#include "core/Registree.h"

#include <QWidget>

class ConfigBaseWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ConfigBaseWidget(Registree config, QWidget* parent = nullptr);

    template <typename T>
    static ConfigBaseWidget* constructWidget(Registree config, QWidget* parent = nullptr)
    {
        return new T(config, parent);
    }

    Registree&   config();
    virtual void commit() = 0;

protected:
    Registree _config;
signals:
};

#endif // CONFIGBASEWIDGET_H
