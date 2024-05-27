#ifndef TERMINALMODULE_H
#define TERMINALMODULE_H

#include "core/Module.h"

class TerminalModule : public Module
{
public:
    TerminalModule();

protected:
    bool _startup() override;
    bool _shutdown() override;
};

#endif // TERMINALMODULE_H
