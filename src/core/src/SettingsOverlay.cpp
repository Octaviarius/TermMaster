#include <Termaster/Core/SettingsOverlay.h>
#include <utility>

namespace
{
bool validKey(const QString& key)
{
    if (key.isEmpty() || key.count('/') >= 64 || key.contains(QChar::Null))
    {
        return false;
    }
    for (const auto& part : key.split('/'))
    {
        if (part.isEmpty() || part == "." || part == "..")
        {
            return false;
        }
    }
    return true;
}

QVariantMap merge(QVariantMap base, const QVariantMap& overrides)
{
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
    {
        if (it.value().typeId() == QMetaType::QVariantMap && base.value(it.key()).typeId() == QMetaType::QVariantMap)
        {
            base.insert(it.key(), merge(base.value(it.key()).toMap(), it.value().toMap()));
        }
        else
        {
            base.insert(it.key(), it.value());
        }
    }
    return base;
}

QVariant lookup(const QVariantMap& root, const QString& key, bool* found)
{
    QVariant current = root;
    for (const auto& part : key.split('/'))
    {
        if (current.typeId() != QMetaType::QVariantMap || !current.toMap().contains(part))
        {
            *found = false;
            return {};
        }
        current = current.toMap().value(part);
    }
    *found = true;
    return current;
}
} // namespace

SettingsOverlay::SettingsOverlay(Settings::Node base, Settings::Node overrides) :
    _base(std::move(base)), _overrides(std::move(overrides))
{
}

SettingsOverlay::SettingsOverlay(SettingsOverlay base, Settings::Node overrides) :
    _base(overrides), _inherited(std::make_shared<SettingsOverlay>(std::move(base))), _overrides(std::move(overrides))
{
}

bool SettingsOverlay::isValid() const
{
    if (!_validPath || !(_inherited ? _inherited->isValid() : _base.isValid()) || !_overrides.isValid())
    {
        return false;
    }
    if (_prefix.isEmpty())
    {
        return true;
    }
    QVariant current = merge(_baseValues(), _overrides.values());
    for (const auto& part : _prefix.split('/'))
    {
        if (current.typeId() != QMetaType::QVariantMap)
        {
            return false;
        }
        const auto map = current.toMap();
        if (!map.contains(part))
        {
            return true;
        }
        current = map.value(part);
    }
    return current.typeId() == QMetaType::QVariantMap;
}

bool SettingsOverlay::contains(const QString& key) const
{
    if (!isValid() || !validKey(_key(key)) || !validKey(key))
    {
        return false;
    }
    bool found = false;
    lookup(merge(_baseValues(), _overrides.values()), _key(key), &found);
    return found;
}

QVariant SettingsOverlay::value(const QString& key, const QVariant& fallback) const
{
    if (!isValid() || !validKey(key) || !validKey(_key(key)))
    {
        return fallback;
    }
    bool       found  = false;
    const auto result = lookup(merge(_baseValues(), _overrides.values()), _key(key), &found);
    return found ? result : fallback;
}

QVariantMap SettingsOverlay::values() const
{
    if (!isValid())
    {
        return {};
    }
    const auto merged = merge(_baseValues(), _overrides.values());
    if (_prefix.isEmpty())
    {
        return merged;
    }
    bool found = false;
    return lookup(merged, _prefix, &found).toMap();
}

bool SettingsOverlay::setValue(const QString& key, const QVariant& value, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!isValid() || !validKey(key) || !validKey(_key(key)))
    {
        if (error)
        {
            *error = "Invalid or expired settings overlay scope or key";
        }
        return false;
    }
    return _overrides.setValue(_key(key), value, error);
}

bool SettingsOverlay::remove(const QString& key, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!isValid() || !validKey(key) || !validKey(_key(key)))
    {
        if (error)
        {
            *error = "Invalid or expired settings overlay scope or key";
        }
        return false;
    }
    return _overrides.remove(_key(key), error);
}

SettingsOverlay SettingsOverlay::node(const QString& path) const
{
    auto scoped = *this;
    if (!path.isEmpty())
    {
        scoped._prefix    = _key(path);
        scoped._validPath = _validPath && validKey(path) && validKey(scoped._prefix);
    }
    return scoped;
}

QVariantMap SettingsOverlay::_baseValues() const
{
    return _inherited ? _inherited->values() : _base.values();
}

QString SettingsOverlay::_key(const QString& key) const
{
    return _prefix.isEmpty() ? key : _prefix + '/' + key;
}
