#pragma once
#include <QVariant>
#include <QWidget>

/** Qt-owned editor extension point. Editors and registry belong to the GUI thread. */
class SettingsFieldEditor : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual QVariant value() const                   = 0;
    virtual void     setValue(const QVariant& value) = 0;
    virtual bool     hasAcceptableInput() const
    {
        return true;
    }
    virtual QString inputError() const
    {
        return {};
    }
    Q_SIGNAL void valueChanged();
};
