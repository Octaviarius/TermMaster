#pragma once
#include <QPointer>
#include <Termaster/Core/gui/settings/SettingsFormModel.h>
#include <Termaster/Core/gui/settings/SettingsWidgetRegistry.h>
class QLabel;
class QCheckBox;
class QFormLayout;
class QGroupBox;

/** Scrollable view; model and registry are borrowed and must share the GUI thread. */
class SettingsFormWidget : public QWidget
{
    Q_OBJECT
public:
    SettingsFormWidget(SettingsFormModel& model, SettingsWidgetRegistry& registry, QWidget* parent = nullptr);
    /** Filters presentation without modifying the draft or validation state. */
    void                 setFilter(const QString& text);
    bool                 hasMatches() const;
    QStringList          visibleFields() const;
    SettingsFieldEditor* editor(const QString& key) const;

private:
    struct Group
    {
        QString      title;
        QGroupBox*   box  = nullptr;
        QFormLayout* form = nullptr;
    };
    struct Row
    {
        SettingsSchema::Field field;
        QWidget*              container = nullptr;
        bool                  visible   = true;
    };
    QHash<QString, Group>                _groups;
    QHash<QString, Row>                  _rows;
    QString                              _filter;
    QLabel*                              _empty      = nullptr;
    bool                                 _hasMatches = false;
    QPointer<SettingsFormModel>          _model;
    QHash<QString, SettingsFieldEditor*> _editors;
    QHash<QString, QLabel*>              _errors;
    QHash<QString, QCheckBox*>           _presence;
    QLabel*                              _summary  = nullptr;
    bool                                 _updating = false;
    void                                 _refresh(const QString& key);
    void                                 _refreshErrors();
    void                                 _applyFilter();
};
