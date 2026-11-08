#pragma once

#include <QString>
#include <QStringList>
#include <QtPlugin>

/** Common root contract. All lifecycle calls execute on the manager's thread. */
class Plugin
{
public:
    struct Info
    {
        QString     id;
        QString     name;
        QString     version;
        QStringList dependencies;
    };

    enum class State
    {
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed
    };

    virtual ~Plugin() = default;

    /** Failure may leave resources allocated; shutdown must also handle failed startup. */
    virtual bool startup(QString* error = nullptr)   = 0;
    /** Return true only after all work, external objects and callbacks have been drained. */
    virtual bool  shutdown(QString* error = nullptr) = 0;
    virtual Info  info() const                       = 0;
    virtual State state() const                      = 0;
};

Q_DECLARE_INTERFACE(Plugin, "org.octaviarius.Termaster.Plugin/1.0")
