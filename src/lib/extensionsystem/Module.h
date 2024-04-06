#ifndef MODULE_H
#define MODULE_H

#include "utils/Path.h"

#include <QtPlugin>

#define Module_IID "org.Octaviarius.Module"

class Module
{
public:
    virtual ~Module() = default;

    virtual bool startup()  = 0;
    virtual bool shutdown() = 0;
};

Q_DECLARE_INTERFACE(Module, Module_IID)

#endif // MODULE_H
