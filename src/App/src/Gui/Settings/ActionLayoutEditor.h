#pragma once

#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>

class QComboBox;
class QListView;
class QPushButton;
class QStandardItemModel;

/** Ordered action selector draft; settings persistence remains with the form owner. */
class ActionLayoutEditor final : public SettingsFieldEditor
{
    Q_OBJECT
public:
    explicit ActionLayoutEditor(QWidget* parent = nullptr);
    QVariant value() const override;
    void     setValue(const QVariant& value) override;

private:
    QStandardItemModel* _model;
    QListView*          _list;
    QComboBox*          _selector;
    QPushButton*        _add;
    QPushButton*        _remove;
    QPushButton*        _up;
    QPushButton*        _down;

    QString _pendingSelector() const;
    QString _title(const QString& selector) const;
    void    _append(const QVariant& selector);
    void    _move(int offset);
    void    _refresh();
};
