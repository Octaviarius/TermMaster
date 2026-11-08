#pragma once

#include <QWidget>
#include <Termaster/Core/Plugin/PluginHost.h>

class QComboBox;

/** Editable positive baud rate; suggestions never constrain device configuration. */
class BaudRateSelector final : public QWidget, public PluginHost::FieldEditor
{
public:
    explicit BaudRateSelector(const QVariant& customRates, QWidget* parent = nullptr);
    QWidget* widget() override;
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    void     setChangedCallback(std::function<void()> callback) override;
    QString  inputError() const override;

private:
    QComboBox*            _combo;
    QVariant              _value;
    bool                  _untouched = true;
    std::function<void()> _changed;
};
