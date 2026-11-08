#pragma once
#include <QPointer>
#include <Termaster/Core/gui/settings/SettingsFormModel.h>
#include <Termaster/Core/gui/settings/SettingsWidgetRegistry.h>
class QLabel;
class QCheckBox;

/** Scrollable view; model and registry are borrowed and must share the GUI thread. */
class SettingsFormWidget : public QWidget
{
    Q_OBJECT
public:
    SettingsFormWidget(SettingsFormModel& model, SettingsWidgetRegistry& registry, QWidget* parent = nullptr);
    SettingsFieldEditor* editor(const QString& key) const;

private:
    QPointer<SettingsFormModel>          _model;
    QHash<QString, SettingsFieldEditor*> _editors;
    QHash<QString, QLabel*>              _errors;
    QHash<QString, QCheckBox*>           _presence;
    QLabel*                              _summary  = nullptr;
    bool                                 _updating = false;
    void                                 _refresh(const QString& key);
    void                                 _refreshErrors();
};
