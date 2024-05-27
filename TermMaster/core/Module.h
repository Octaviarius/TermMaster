#ifndef MODULE_H
#define MODULE_H

#include <QSet>
#include <QString>
#include <QVector>
#include <QVersionNumber>

using QStringSet = QSet<QString>;

class Module;
class ModuleApi;

struct ModuleInfo
{
    QString        name;
    QString        description;
    QString        author;
    QVersionNumber version;
    bool           isStarted;
};

class ModuleApi
{
public:
    ModuleApi(Module* module) : _module(module)
    {
    }
    virtual ~ModuleApi() = default;

    const ModuleInfo info();

private:
    Module* _module;
};

class Module
{
public:
    Module() : _api(nullptr), _isStartedUp(false)
    {
    }

    ~Module()
    {
    }

    bool startup()
    {
        if (!_isStartedUp)
        {
            _isStartedUp = _startup();
        }
        return _isStartedUp;
    };

    bool shutdown()
    {
        if (_isStartedUp)
        {
            _isStartedUp = !_shutdown();
        }
        return !_isStartedUp;
    };

    QString name() const
    {
        return _name;
    }

    QString description() const
    {
        return _description;
    }

    QString author() const
    {
        return _author;
    }

    QStringSet dependecies() const
    {
        return _depModules;
    }

    bool isStartedUp() const
    {
        return _isStartedUp;
    }

    QVersionNumber version() const
    {
        return _version;
    }

    const ModuleInfo info() const
    {
        ModuleInfo info;
        info.name        = _name;
        info.description = _description;
        info.author      = _author;
        info.version     = _version;
        info.isStarted   = _isStartedUp;
        return info;
    }

    ModuleApi* API() const
    {
        return _api;
    }

protected:
    QString        _name;
    QString        _description;
    QString        _author;
    QVersionNumber _version;
    QStringSet     _depModules;
    ModuleApi*     _api;
    bool           _isStartedUp;

    template <typename T, typename... Args>
    void _allocateApi(Args... args)
    {
        if (_api == nullptr)
        {
            _api = new T(this, args...);
        }
    }

    void _cleanupApi()
    {
        if (_api != nullptr)
        {
            delete _api;
            _api = nullptr;
        }
    }

    virtual bool _startup()  = 0;
    virtual bool _shutdown() = 0;
};

#endif
