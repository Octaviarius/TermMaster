#include "ITerminal.h"

ITerminal::ITerminal(QObject* parent) : QObject(parent)
{
}

ITerminal::State ITerminal::state() const
{
    return _state;
}

void ITerminal::_setState(ITerminal::State state)
{
    if (_state != state)
    {
        _state = state;
        emit stateChanged(_state);
    }
}
