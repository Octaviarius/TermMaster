#pragma once

#include <QObject>
#include <QStringList>
#include <Termaster/Core/Settings.h>
#include <functional>

/** Shared ordered processor shortcuts. Missing catalog entries remain valid preferences. */
class ProcessorFavorites final : public QObject
{
    Q_OBJECT
public:
    explicit ProcessorFavorites(Settings::Node                preferences,
                                std::function<bool(QString*)> persist = {},
                                QObject*                      parent  = nullptr);
    QStringList   ids() const;
    bool          contains(const QString& id) const;
    bool          setIds(QStringList ids, QString* error = nullptr);
    bool          setFavorite(const QString& id, bool favorite, QString* error = nullptr);
    bool          move(const QString& id, int delta, QString* error = nullptr);
    Q_SIGNAL void changed();

private:
    Settings::Node                _preferences;
    std::function<bool(QString*)> _persist;
    QStringList                   _ids;
    bool                          _saving = false;

    static bool _validate(const QStringList& ids, QString* error);
};
