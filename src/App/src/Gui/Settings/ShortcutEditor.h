#pragma once
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
class CommandRegistry;
class CommandShortcuts;
class ShortcutModel;
class QKeySequenceEdit;
class QLabel;
class QTableView;
class QSortFilterProxyModel;
/** Application shortcut draft. Commands are addressed by stable ID, never retained. */
class ShortcutEditor final : public SettingsFieldEditor
{
    Q_OBJECT
public:
    ShortcutEditor(CommandRegistry& registry, CommandShortcuts& shortcuts, QWidget* parent);
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    bool     hasAcceptableInput() const override;
    QString  inputError() const override;

private:
    CommandShortcuts&      _shortcuts;
    ShortcutModel*         _model;
    QSortFilterProxyModel* _filter;
    QTableView*            _table;
    QKeySequenceEdit*      _sequence;
    QLabel*                _error;
    QString                _selectedId() const;
    void                   _selectionChanged();
    void                   _changed();
};
