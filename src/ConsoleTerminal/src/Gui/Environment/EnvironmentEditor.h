#pragma once

#include <QProcessEnvironment>
#include <QWidget>
#include <Termaster/Core/Plugin/PluginHost.h>

class QLabel;
class QTableView;

/** GUI-thread draft editor. Only explicit overrides cross the settings ABI. */
class EnvironmentEditor : public QWidget, public PluginHost::FieldEditor
{
public:
    using BaselineProvider = std::function<QProcessEnvironment(QString*)>;

    explicit EnvironmentEditor(QWidget* parent = nullptr, BaselineProvider provider = {});
    QWidget* widget() override;
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    void     setChangedCallback(std::function<void()> callback) override;
    QString  inputError() const override;
    bool     refreshFromUser(QString* error = nullptr);
    bool     addOverride(const QString& name, const QVariant& value);
    bool     removeVariable(const QString& name);
    bool     resetOverride(const QString& name);

private:
    class Model;
    Model*                _model;
    QTableView*           _table;
    QLabel*               _error;
    BaselineProvider      _provider;
    std::function<void()> _changed;

    void _updateError();
};
