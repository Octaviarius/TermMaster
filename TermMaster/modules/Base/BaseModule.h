#ifndef BASEMODULE_H
#define BASEMODULE_H

#include "core/Module.h"

class BaseModule : public Module
{
public:
    BaseModule();

protected:
    bool _startup() override;
    bool _shutdown() override;
};

#endif // BASEMODULE_H
