#ifndef MODULE_H
#define MODULE_H

#include "utils/Path.h"
#include "utils/utils.h"

#include <QDateTime>
#include <QtPlugin>

#define Module_IID "org.Octaviarius.Module"

#define MODULE_DECLARATION                                                                                             \
public:                                                                                                                \
    QDateTime buildTime() const override                                                                               \
    {                                                                                                                  \
        return BuildTime::compileTime();                                                                               \
    }                                                                                                                  \
                                                                                                                       \
private:

class Module
{
public:
    virtual ~Module() = default;

    virtual bool startup()  = 0;
    virtual bool shutdown() = 0;

    virtual QDateTime buildTime() const = 0;
};

Q_DECLARE_INTERFACE(Module, Module_IID)

#endif // MODULE_H
