#pragma once
#include <QHash>
#include <QPointer>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/SettingsSchema.h>
#include <functional>

/** Factories must remain loaded until all their editors have been destroyed. */
class SettingsWidgetRegistry : public QObject
{
    Q_OBJECT
public:
    using Factory = std::function<SettingsFieldEditor*(const SettingsSchema::Field&, QWidget*)>;
    explicit SettingsWidgetRegistry(QObject* parent = nullptr);
    void                 registerWidget(const QString& name, Factory factory);
    bool                 unregisterWidget(const QString& name);
    SettingsFieldEditor* create(const SettingsSchema::Field& field, QWidget* parent);

private:
    QHash<QString, Factory>                              _factories;
    QHash<QString, QList<QPointer<SettingsFieldEditor>>> _live;
};
