#ifndef COREMODULE_H
#define COREMODULE_H

#include "extensionsystem/Module.h"

class TerminalModule : public QObject, public Module
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID Module_IID FILE "TerminalModule.json")
    Q_INTERFACES(Module)
    MODULE_DECLARATION
public:
    TerminalModule()  = default;
    ~TerminalModule() = default;

    bool startup() override;
    bool shutdown() override;
};

#endif // COREMODULE_H
