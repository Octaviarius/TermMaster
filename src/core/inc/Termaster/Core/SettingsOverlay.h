#pragma once

#include <Termaster/Core/Settings.h>

/** Borrowed merged settings view. Both owners must outlive the view and share its thread. */
class SettingsOverlay
{
public:
    SettingsOverlay(Settings::Node base, Settings::Node overrides);
    SettingsOverlay(SettingsOverlay base, Settings::Node overrides);
    bool            isValid() const;
    bool            contains(const QString& key) const;
    QVariant        value(const QString& key, const QVariant& fallback = {}) const;
    QVariantMap     values() const;
    bool            containsInherited(const QString& key) const;
    QVariant        inheritedValue(const QString& key, const QVariant& fallback = {}) const;
    bool            setValue(const QString& key, const QVariant& value, QString* error = nullptr);
    bool            remove(const QString& key, QString* error = nullptr);
    bool            applyValues(QVariantMap values, QStringList removed, QString* error = nullptr);
    SettingsOverlay node(const QString& path) const;

private:
    Settings::Node                   _base;
    std::shared_ptr<SettingsOverlay> _inherited;
    Settings::Node                   _overrides;
    QString                          _prefix;
    bool                             _validPath = true;
    QVariantMap                      _baseValues() const;
    QString                          _key(const QString& key) const;
};
