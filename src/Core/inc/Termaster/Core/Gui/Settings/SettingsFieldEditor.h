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
    /** Effective draft values keyed by relative settings paths, for auxiliary presentation. */
    virtual void setContext(const QVariantMap& context)
    {
        Q_UNUSED(context)
    }
    virtual bool hasAcceptableInput() const
    {
        return true;
    }
    virtual QString inputError() const
    {
        return {};
    }
    Q_SIGNAL void valueChanged();
    /** Input diagnostics changed without editing the field value. */
    Q_SIGNAL void validationChanged();
};
