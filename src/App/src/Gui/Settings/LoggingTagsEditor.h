#pragma once

#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>

class QStandardItemModel;
class QTableView;
class QToolButton;

/** Bounded tag-level draft. The settings form owns validation and persistence. */
class LoggingTagsEditor final : public SettingsFieldEditor
{
    Q_OBJECT
public:
    explicit LoggingTagsEditor(QWidget* parent = nullptr);
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    bool     hasAcceptableInput() const override;
    QString  inputError() const override;

private:
    QStandardItemModel* _model;
    QTableView*         _table;
    QToolButton*        _add;
    QToolButton*        _remove;
    QVariantMap         _overflow;
    QVariant            _invalidValue;
    bool                _loadedOversize = false;
    bool                _updating       = false;

    void _changed();
    void _updateActions();
};
