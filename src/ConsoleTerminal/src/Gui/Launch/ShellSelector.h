#pragma once

#include <QWidget>
#include <Termaster/Core/Plugin/PluginHost.h>

class QComboBox;

/** Editable shell suggestions. The persisted value is always an executable/path. */
class ShellSelector final : public QWidget, public PluginHost::FieldEditor
{
public:
    explicit ShellSelector(QWidget* parent = nullptr);
    QWidget* widget() override;
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    void     setChangedCallback(std::function<void()> callback) override;
    QString  inputError() const override;

private:
    QComboBox*            _combo;
    QVariant              _value     = QString();
    bool                  _untouched = true;
    std::function<void()> _changed;
};
