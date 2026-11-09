#include "ProcessorFavorites.h"

#include <QPointer>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace
{
bool fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}
} // namespace

ProcessorFavorites::ProcessorFavorites(Settings::Node                preferences,
                                       std::function<bool(QString*)> persist,
                                       QObject*                      parent) :
    QObject(parent), _preferences(std::move(preferences)), _persist(std::move(persist))
{
    if (!_preferences.isValid())
    {
        throw std::invalid_argument("Processor favorites require a valid settings scope");
    }
    _ids = {QStringLiteral("native.bypass")};
    if (_preferences.contains("favorites"))
    {
        const auto value = _preferences.value("favorites");
        if ((value.typeId() != QMetaType::QVariantList && value.typeId() != QMetaType::QStringList)
            || value.toList().size() > 128)
        {
            throw std::invalid_argument("Processor favorites must be a list of at most 128 IDs");
        }
        _ids.clear();
        for (const auto& item : value.toList())
        {
            if (item.typeId() != QMetaType::QString)
            {
                throw std::invalid_argument("Processor favorite IDs must be strings");
            }
            _ids.append(item.toString());
        }
    }
    QString error;
    if (!_validate(_ids, &error))
    {
        throw std::invalid_argument(error.toStdString());
    }
}

QStringList ProcessorFavorites::ids() const
{
    return _ids;
}

bool ProcessorFavorites::contains(const QString& id) const
{
    return _ids.contains(id);
}

bool ProcessorFavorites::setIds(QStringList ids, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (_saving)
    {
        return fail(error, "Processor favorites are already being saved");
    }
    if (!_validate(ids, error))
    {
        return false;
    }
    if (ids == _ids)
    {
        return true;
    }
    if (!_preferences.isValid())
    {
        return fail(error, "Processor favorites settings scope has expired");
    }
    const auto preferences = _preferences;
    auto       writable    = preferences;
    const bool hadValue    = preferences.contains("favorites");
    const auto previous    = preferences.value("favorites");
    if (!writable.setValue("favorites", ids, error))
    {
        return false;
    }
    _saving = true;
    QPointer<ProcessorFavorites> guard(this);
    const auto                   persist = _persist;
    QString                      message;
    bool                         saved = true;
    try
    {
        saved = !persist || persist(&message);
    }
    catch (const std::exception& exception)
    {
        saved   = false;
        message = QString::fromUtf8(exception.what());
    }
    catch (...)
    {
        saved   = false;
        message = QStringLiteral("Processor favorites persistence failed with an unknown exception");
    }
    if (guard)
    {
        guard->_saving = false;
    }
    if (!saved)
    {
        QString    rollback;
        const bool restored = hadValue ? writable.setValue("favorites", previous, &rollback) :
                                         writable.remove("favorites", &rollback);
        if (message.isEmpty())
        {
            message = QStringLiteral("Cannot persist processor favorites");
        }
        if (!restored)
        {
            message += "; preference rollback failed: " + rollback;
        }
        return fail(error, message);
    }
    if (!guard)
    {
        return true;
    }
    _ids = std::move(ids);
    emit changed();
    return true;
}

bool ProcessorFavorites::setFavorite(const QString& id, bool favorite, QString* error)
{
    if (!_validate({id}, error))
    {
        return false;
    }
    auto updated = _ids;
    if (favorite && !updated.contains(id))
    {
        updated.append(id);
    }
    else if (!favorite)
    {
        updated.removeAll(id);
    }
    return setIds(std::move(updated), error);
}

bool ProcessorFavorites::move(const QString& id, int delta, QString* error)
{
    if (error)
    {
        error->clear();
    }
    const auto index = _ids.indexOf(id);
    if (index < 0)
    {
        return fail(error, "Processor favorite is not registered: " + id);
    }
    const auto destination = std::clamp<qint64>(qint64(index) + delta, 0, _ids.size() - 1);
    auto       updated     = _ids;
    updated.move(index, qsizetype(destination));
    return setIds(std::move(updated), error);
}

bool ProcessorFavorites::_validate(const QStringList& ids, QString* error)
{
    if (ids.size() > 128)
    {
        return fail(error, "Processor favorites support at most 128 IDs");
    }
    QSet<QString> seen;
    for (const auto& id : ids)
    {
        if (id.isEmpty() || id != id.trimmed() || id.size() > 256 || id.contains(QChar::Null))
        {
            return fail(error, "Processor favorite IDs must be trimmed strings of 1 to 256 characters without NUL");
        }
        if (seen.contains(id))
        {
            return fail(error, "Processor favorite IDs must be unique");
        }
        seen.insert(id);
    }
    return true;
}
