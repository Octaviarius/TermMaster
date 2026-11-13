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
    const auto result = _lookup(_prefix, false);
    return !result.blocked && (!result.found || result.mapping);
}

bool SettingsOverlay::contains(const QString& key) const
{
    if (!isValid() || !validKey(_key(key)) || !validKey(key))
    {
        return false;
    }
    return _lookup(_key(key), false).found;
}

QVariant SettingsOverlay::value(const QString& key, const QVariant& fallback) const
{
    if (!isValid() || !validKey(key) || !validKey(_key(key)))
    {
        return fallback;
    }
    const auto result = _lookup(_key(key), true);
    return result.found ? result.value : fallback;
}

bool SettingsOverlay::containsOverride(const QString& key) const
{
    return isValid() && validKey(key) && validKey(_key(key)) && _overrides.contains(_key(key));
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

bool SettingsOverlay::containsInherited(const QString& key) const
{
    if (!isValid() || !validKey(key) || !validKey(_key(key)))
    {
        return false;
    }
    bool found = false;
    lookup(_baseValues(), _key(key), &found);
    return found;
}

QVariant SettingsOverlay::inheritedValue(const QString& key, const QVariant& fallback) const
{
    if (!isValid() || !validKey(key) || !validKey(_key(key)))
    {
        return fallback;
    }
    bool       found  = false;
    const auto result = lookup(_baseValues(), _key(key), &found);
    return found ? result : fallback;
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

bool SettingsOverlay::applyValues(QVariantMap values, QStringList removed, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!isValid())
    {
        if (error)
        {
            *error = "Invalid or expired settings overlay scope";
        }
        return false;
    }
    QVariantMap prefixed;
    QStringList prefixedRemoved;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        if (!validKey(it.key()) || !validKey(_key(it.key())))
        {
            if (error)
            {
                *error = "Invalid settings key";
            }
            return false;
        }
        prefixed.insert(_key(it.key()), it.value());
    }
    for (const auto& key : removed)
    {
        if (!validKey(key) || !validKey(_key(key)))
        {
            if (error)
            {
                *error = "Invalid settings key";
            }
            return false;
        }
        prefixedRemoved.append(_key(key));
    }
    return _overrides.applyValues(prefixed, prefixedRemoved, error);
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

SettingsOverlay::Lookup SettingsOverlay::_lookup(const QString& key, bool materialize) const
{
    const auto read = [&key, materialize](const Settings::Node& node) {
        Lookup     result;
        QString    prefix;
        const auto parts = key.split('/');
        for (qsizetype index = 0; index < parts.size(); ++index)
        {
            prefix = prefix.isEmpty() ? parts[index] : prefix + '/' + parts[index];
            if (!node.contains(prefix))
            {
                return result;
            }
            const bool mapping = node.node(prefix).isValid();
            if (index + 1 < parts.size())
            {
                if (!mapping)
                {
                    result.blocked      = true;
                    result.blockedDepth = index + 1;
                    return result;
                }
                result.mappingDepth = index + 1;
                continue;
            }
            result.found   = true;
            result.mapping = mapping;
            if (materialize)
            {
                result.value = node.value(key);
            }
        }
        return result;
    };

    auto result = read(_overrides);
    if (result.blocked || (result.found && (!result.mapping || !materialize)))
    {
        return result;
    }

    auto base = _inherited ? _inherited->_lookup(_inherited->_key(key), materialize) : read(_base);
    if (_inherited && base.blocked && !_inherited->_prefix.isEmpty())
    {
        base.blockedDepth -= _inherited->_prefix.count('/') + 1;
    }
    if (!result.found)
    {
        // A higher mapping replaces a lower scalar at the same ancestor.
        if (base.blocked && base.blockedDepth <= result.mappingDepth)
        {
            return result;
        }
        return base;
    }
    if (base.found && base.mapping)
    {
        result.value = merge(base.value.toMap(), result.value.toMap());
    }
    return result;
}

QVariantMap SettingsOverlay::_baseValues() const
{
    return _inherited ? _inherited->values() : _base.values();
}

QString SettingsOverlay::_key(const QString& key) const
{
    return _prefix.isEmpty() ? key : _prefix + '/' + key;
}
