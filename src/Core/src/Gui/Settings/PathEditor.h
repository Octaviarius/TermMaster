#pragma once

#include <QPointer>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/SettingsSchema.h>

class QFileDialog;
class QLineEdit;
class QToolButton;

/** Editable path draft with a standard file or directory chooser; never writes files. */
class PathEditor final : public SettingsFieldEditor
{
public:
    explicit PathEditor(const SettingsSchema::Field& field, QWidget* parent = nullptr);
    QVariant value() const override;
    void     setValue(const QVariant& value) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLineEdit*            _line;
    QToolButton*          _browse;
    QPointer<QFileDialog> _dialog;
    QVariant              _value;
    bool                  _untouched = true;
    bool                  _directory;
    bool                  _save;
    QString               _title;
    QString               _filter;

    void _choose();
    void _alignButton();
};
