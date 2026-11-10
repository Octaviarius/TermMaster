#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <stdexcept>
#include <utility>

namespace
{
int integerCategory(int type)
{
    switch (type)
    {
        case QMetaType::Char:
        case QMetaType::SChar:
        case QMetaType::Short:
        case QMetaType::Int:
        case QMetaType::Long:
        case QMetaType::LongLong:
            return 1;
        case QMetaType::UChar:
        case QMetaType::UShort:
        case QMetaType::UInt:
        case QMetaType::ULong:
        case QMetaType::ULongLong:
            return 2;
        default:
            return 0;
    }
}
bool sameValue(const QVariant& a, const QVariant& b)
{
    const int category = integerCategory(a.typeId());
    if (category && category == integerCategory(b.typeId()))
    {
        return category == 1 ? a.toLongLong() == b.toLongLong() : a.toULongLong() == b.toULongLong();
    }
    if (a.typeId() != b.typeId())
    {
        return false;
    }
    if (a.typeId() == QMetaType::QVariantMap)
    {
        const auto left  = a.toMap();
        const auto right = b.toMap();
        if (left.keys() != right.keys())
        {
            return false;
        }
        for (auto it = left.cbegin(); it != left.cend(); ++it)
        {
            if (!sameValue(it.value(), right.value(it.key())))
            {
                return false;
            }
        }
        return true;
    }
    if (a.typeId() == QMetaType::QVariantList)
    {
        const auto left  = a.toList();
        const auto right = b.toList();
        if (left.size() != right.size())
        {
            return false;
        }
        for (qsizetype i = 0; i < left.size(); ++i)
        {
            if (!sameValue(left[i], right[i]))
            {
                return false;
            }
        }
        return true;
    }
    return a == b;
}
} // namespace

SettingsFormModel::SettingsFormModel(std::shared_ptr<const SettingsSchema> schema,
                                     Settings::Node                        target,
                                     QObject* parent) : QObject(parent), _schema(std::move(schema))
{
    if (!_schema)
    {
        throw std::invalid_argument("Settings schema is null");
    }
    _valid    = [target] { return target.isValid(); };
    _contains = [target](const QString& key) { return target.contains(key); };
    _read     = [target](const QString& key) { return target.value(key); };
    _patch    = [target](const QVariantMap& values, const QStringList& removed, QString* error) mutable {
        return target.applyValues(values, removed, error);
    };
    _hasInherited  = [](const QString&) { return false; };
    _readInherited = [](const QString&) { return QVariant(); };
    _hasLocal      = _contains;
    reload();
}

SettingsFormModel::SettingsFormModel(std::shared_ptr<const SettingsSchema> schema,
                                     SettingsOverlay                       target,
                                     QObject* parent) : QObject(parent), _schema(std::move(schema))
{
    if (!_schema)
    {
        throw std::invalid_argument("Settings schema is null");
    }
    _valid    = [target] { return target.isValid(); };
    _contains = [target](const QString& key) { return target.contains(key); };
    _read     = [target](const QString& key) { return target.value(key); };
    _patch    = [target](const QVariantMap& values, const QStringList& removed, QString* error) mutable {
        return target.applyValues(values, removed, error);
    };
    _supportsInheritance = true;
    _hasLocal            = [target](const QString& key) { return target.containsOverride(key); };
    _hasInherited        = [target](const QString& key) { return target.containsInherited(key); };
    _readInherited       = [target](const QString& key) { return target.inheritedValue(key); };
    reload();
}

const SettingsSchema& SettingsFormModel::schema() const
{
    return *_schema;
}
QVariant SettingsFormModel::value(const QString& key) const
{
    return _draft.value(key);
}
bool SettingsFormModel::contains(const QString& key) const
{
    return _draft.contains(key);
}

bool SettingsFormModel::isActive(const QString& key) const
{
    return _schema->isActive(key, _draft);
}

QVariantMap SettingsFormModel::values() const
{
    return _schema->activeValues(_draft);
}

bool SettingsFormModel::supportsInheritance() const
{
    return _supportsInheritance;
}
bool SettingsFormModel::hasOverride(const QString& key) const
{
    return _local.contains(key);
}

bool SettingsFormModel::canReset(const QString& key) const
{
    const auto* field = _field(key);
    return field && !field->readOnly && _supportsInheritance
        && (!field->required || _hasInherited(field->settingsPath) || field->hasDefault);
}
QVariant SettingsFormModel::inheritedValue(const QString& key) const
{
    const auto* field = _field(key);
    if (!field)
    {
        return {};
    }
    if (_hasInherited(field->settingsPath))
    {
        return _readInherited(field->settingsPath);
    }
    return field->hasDefault ? field->defaultValue : QVariant();
}
void SettingsFormModel::setValue(const QString& key, const QVariant& value)
{
    const auto* field = _field(key);
    if (!field || field->readOnly)
    {
        return;
    }
    const bool wasDirty = isDirty();
    _removed.remove(key);
    _local.insert(key);
    _draft.insert(key, value);
    _inputErrors.remove(key);
    _changed(key, wasDirty);
}

void SettingsFormModel::removeValue(const QString& key)
{
    const auto* field = _field(key);
    if (!field || field->readOnly || (field->required && !canReset(key)))
    {
        return;
    }
    const bool wasDirty = isDirty();
    _local.remove(key);
    if (!_supportsInheritance || _originalLocal.contains(key))
    {
        _removed.insert(key);
    }
    else
    {
        _removed.remove(key);
    }
    if (_hasInherited(field->settingsPath))
    {
        _draft.insert(key, _readInherited(field->settingsPath));
    }
    else if (_supportsInheritance && field->hasDefault)
    {
        _draft.insert(key, field->defaultValue);
    }
    else
    {
        _draft.remove(key);
    }
    _inputErrors.remove(key);
    _changed(key, wasDirty);
}

QList<SettingsSchema::Error> SettingsFormModel::errors() const
{
    return _errors;
}
bool SettingsFormModel::isDirty() const
{
    for (const auto& field : _schema->fields())
    {
        if (isActive(field.key)
            && (_edited.contains(field.key) || _removed.contains(field.key) || _inputErrors.contains(field.key)))
        {
            return true;
        }
    }
    return false;
}
bool SettingsFormModel::isValid() const
{
    return _errors.isEmpty() && _valid();
}

void SettingsFormModel::reload()
{
    const bool wasDirty = isDirty();
    _original           = _current();
    _draft              = _original;
    _local.clear();
    for (const auto& field : _schema->fields())
    {
        if (_hasLocal(field.settingsPath))
        {
            _local.insert(field.key);
        }
    }
    _originalLocal = _local;
    _edited.clear();
    _removed.clear();
    _inputErrors.clear();
    for (const auto& field : _schema->fields())
    {
        if (!_draft.contains(field.key) && field.hasDefault)
        {
            _draft.insert(field.key, field.defaultValue);
        }
    }
    _validate();
    for (const auto& field : _schema->fields())
    {
        emit fieldChanged(field.key);
    }
    if (wasDirty)
    {
        emit dirtyChanged(false);
    }
}

bool SettingsFormModel::apply(QString* error)
{
    if (error)
    {
        error->clear();
    }
    auto fail = [error](const QString& message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (!_valid())
    {
        return fail("Settings scope is invalid or expired");
    }
    for (auto it = _inputErrors.cbegin(); it != _inputErrors.cend(); ++it)
    {
        if (isActive(it.key()))
        {
            return fail("Correct invalid editor input before applying settings");
        }
    }

    auto        current  = _current();
    auto        merged   = current;
    const auto* selector = _field(_schema->discriminator());
    if (selector
        && ((_supportsInheritance && _hasLocal(selector->settingsPath) != _originalLocal.contains(selector->key))
            || current.contains(selector->key) != _original.contains(selector->key)
            || !sameValue(current.value(selector->key), _original.value(selector->key))))
    {
        return fail("Settings changed externally at " + selector->settingsPath + "; reload before applying");
    }
    QVariantMap patch;
    QStringList removed;
    for (const auto& field : _schema->fields())
    {
        if (!isActive(field.key))
        {
            // Inherited inactive values remain in their owner. Only this scope's known values are removed.
            if (_hasLocal(field.settingsPath))
            {
                if (!_originalLocal.contains(field.key) || current.contains(field.key) != _original.contains(field.key)
                    || !sameValue(current.value(field.key), _original.value(field.key)))
                {
                    return fail("Settings changed externally at " + field.settingsPath + "; reload before applying");
                }
                removed.append(field.settingsPath);
            }
            merged.remove(field.key);
            continue;
        }
        if (field.readOnly)
        {
            continue;
        }
        const bool defaultPending = !_supportsInheritance && !_original.contains(field.key) && field.hasDefault
                                 && _draft.contains(field.key);
        if (!_edited.contains(field.key) && !_removed.contains(field.key) && !defaultPending)
        {
            continue;
        }
        if ((_supportsInheritance && _hasLocal(field.settingsPath) != _originalLocal.contains(field.key))
            || current.contains(field.key) != _original.contains(field.key)
            || !sameValue(current.value(field.key), _original.value(field.key)))
        {
            return fail("Settings changed externally at " + field.settingsPath + "; reload before applying");
        }
        if (_removed.contains(field.key))
        {
            removed.append(field.settingsPath);
            if (_hasInherited(field.settingsPath))
            {
                merged.insert(field.key, _readInherited(field.settingsPath));
            }
            else
            {
                merged.remove(field.key);
            }
        }
        else if (_draft.contains(field.key))
        {
            merged.insert(field.key, _draft.value(field.key));
            patch.insert(field.settingsPath, _draft.value(field.key));
        }
        else
        {
            merged.remove(field.key);
            removed.append(field.settingsPath);
        }
    }
    if (_supportsInheritance)
    {
        for (const auto& field : _schema->fields())
        {
            if (!merged.contains(field.key) && field.hasDefault)
            {
                merged.insert(field.key, field.defaultValue);
            }
        }
    }
    const auto validation = _schema->validate(_schema->activeValues(merged));
    if (!validation.isEmpty())
    {
        return fail(validation.first().path + ": " + validation.first().message);
    }
    if (!_patch(patch, removed, error))
    {
        return false;
    }
    reload();
    return true;
}

void SettingsFormModel::setInputError(const QString& key, const QString& error)
{
    const auto* field = _field(key);
    if (!field || field->readOnly)
    {
        return;
    }
    const bool wasDirty = isDirty();
    if (error.isEmpty())
    {
        _inputErrors.remove(key);
    }
    else
    {
        _inputErrors.insert(key, error);
    }
    _validate();
    if (wasDirty != isDirty())
    {
        emit dirtyChanged(isDirty());
    }
}

const SettingsSchema::Field* SettingsFormModel::_field(const QString& key) const
{
    for (const auto& field : _schema->fields())
    {
        if (field.key == key)
        {
            return &field;
        }
    }
    return nullptr;
}

QVariantMap SettingsFormModel::_current() const
{
    QVariantMap document;
    for (const auto& field : _schema->fields())
    {
        if (_contains(field.settingsPath))
        {
            document.insert(field.key, _read(field.settingsPath));
        }
    }
    return document;
}

void SettingsFormModel::_validate()
{
    _errors = _schema->validate(values());
    for (auto it = _inputErrors.cbegin(); it != _inputErrors.cend(); ++it)
    {
        if (isActive(it.key()))
        {
            _errors.append({it.key(), it.value()});
        }
    }
    emit validationChanged();
}

void SettingsFormModel::_changed(const QString& key, bool wasDirty)
{
    const auto* field           = _field(key);
    const bool  defaultOriginal = _supportsInheritance && !_original.contains(key) && field && field->hasDefault;
    if ((!_supportsInheritance || _local.contains(key) == _originalLocal.contains(key))
        && _draft.contains(key) == (_original.contains(key) || defaultOriginal)
        && sameValue(_draft.value(key), defaultOriginal ? field->defaultValue : _original.value(key)))
    {
        _edited.remove(key);
    }
    else
    {
        _edited.insert(key);
    }
    _validate();
    emit fieldChanged(key);
    if (wasDirty != isDirty())
    {
        emit dirtyChanged(isDirty());
    }
}
