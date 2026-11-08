#pragma once

#include <Termaster/Core/Settings.h>

/** Borrowed merged settings view. Both owners must outlive the view and share its thread. */
class SettingsOverlay
{
public:
    SettingsOverlay(Settings::Node base, Settings::Node overrides);
    bool            isValid() const;
    bool            contains(const QString& key) const;
    QVariant        value(const QString& key, const QVariant& fallback = {}) const;
    QVariantMap     values() const;
    bool            setValue(const QString& key, const QVariant& value, QString* error = nullptr);
    bool            remove(const QString& key, QString* error = nullptr);
    SettingsOverlay node(const QString& path) const;

private:
    Settings::Node _base;
    Settings::Node _overrides;
    QString        _prefix;
    bool           _validPath = true;
    QString        _key(const QString& key) const;
};
