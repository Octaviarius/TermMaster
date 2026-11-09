#pragma once

#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>

class FavoritesModel;
class QLabel;
class QTableView;

/** Scoped favorites draft; persistence remains with the settings form owner. */
class FavoritesEditor final : public SettingsFieldEditor
{
    Q_OBJECT
public:
    explicit FavoritesEditor(QWidget* parent = nullptr);
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    bool     hasAcceptableInput() const override;
    QString  inputError() const override;

private:
    FavoritesModel* _model;
    QTableView*     _table;
    QLabel*         _error;

    void _changed();
};
