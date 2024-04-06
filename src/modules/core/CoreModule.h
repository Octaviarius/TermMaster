#ifndef COREMODULE_H
#define COREMODULE_H

#include "extensionsystem/Module.h"

class CoreModule : public QObject, public Module
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID Module_IID FILE "CoreModule.json")
    Q_INTERFACES(Module)
public:
    CoreModule()  = default;
    ~CoreModule() = default;

    bool startup() override;
    bool shutdown() override;
};

#endif // COREMODULE_H
