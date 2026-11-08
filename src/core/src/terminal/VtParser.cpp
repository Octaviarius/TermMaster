#include <Termaster/Core/terminal/VtParser.h>
#include <limits>
#include <utility>

void VtParser::receive(QByteArrayView bytes, const Handler& handler)
{
    QByteArray text;
    const auto flush = [&] {
        if (!text.isEmpty())
        {
            Sequence sequence;
            sequence.data = std::move(text);
            handler(sequence);
            text.clear();
        }
    };
    const auto execute = [&](char byte) {
        Sequence sequence;
        sequence.kind    = Kind::Execute;
        sequence.command = byte;
        handler(sequence);
    };

    for (char byte : bytes)
    {
        const auto value = static_cast<unsigned char>(byte);
        if (_state == State::Ground && value >= 0x20 && value != 0x7f
            && !(_eightBitControls && value >= 0x80 && value <= 0x9f))
        {
            text.append(byte);
            if (text.size() == 65536)
            {
                flush();
            }
            continue;
        }
        flush();

        if (value == 0x18 || value == 0x1a)
        {
            _abort();
            execute(byte);
            continue;
        }
        // ESC starts a new sequence unless it completes ST. Oversized strings
        // remain quarantined until their terminator to avoid leaking payload.
        if (_state == State::StringEscape && byte != '\\' && !_overflow && !(_eightBitControls && value == 0x9c))
        {
            _start(State::Escape, Kind::Escape);
        }
        const bool string = _state == State::String || _state == State::StringEscape;
        if (string)
        {
            if ((_eightBitControls && value == 0x9c) || (_sequence.kind == Kind::Osc && value == 0x07)
                || (_state == State::StringEscape && byte == '\\'))
            {
                _emit(handler);
            }
            else if (value == 0x1b)
            {
                if (_state == State::StringEscape)
                {
                    _appendString('\x1b');
                }
                _state = State::StringEscape;
            }
            else
            {
                if (_state == State::StringEscape)
                {
                    _appendString('\x1b');
                }
                _appendString(byte);
                _state = State::String;
            }
            continue;
        }
        if (value == 0x1b)
        {
            _start(State::Escape, Kind::Escape);
            continue;
        }
        if (_eightBitControls && value >= 0x80 && value <= 0x9f)
        {
            if (value == 0x9b)
            {
                _start(State::Csi, Kind::Csi);
            }
            else if (value == 0x9d)
            {
                _start(State::String, Kind::Osc);
            }
            else if (value == 0x90)
            {
                _start(State::DcsHeader, Kind::Dcs);
            }
            else
            {
                _abort();
                execute(byte);
            }
            continue;
        }
        if (value < 0x20)
        {
            execute(byte);
            continue;
        }
        if (value == 0x7f)
        {
            continue;
        }
        if (_state == State::Ground)
        {
            continue;
        }
        if (_state == State::Ignore)
        {
            if (value >= 0x40 && value <= 0x7e)
            {
                if (_sequence.kind == Kind::Dcs)
                {
                    _state = State::String;
                }
                else
                {
                    _abort();
                }
            }
            continue;
        }
        if (++_headerBytes > _headerLimit)
        {
            _discard();
            _state = State::Ignore;
            if (value >= 0x40 && value <= 0x7e)
            {
                if (_sequence.kind == Kind::Dcs)
                {
                    _state = State::String;
                }
                else
                {
                    _abort();
                }
            }
            continue;
        }
        if (_state == State::Escape && _sequence.intermediates.isEmpty())
        {
            if (byte == '[')
            {
                _start(State::Csi, Kind::Csi);
                continue;
            }
            if (byte == ']' || byte == 'P' || byte == 'X' || byte == '^' || byte == '_')
            {
                _start(byte == 'P' ? State::DcsHeader : State::String,
                       byte == ']' ? Kind::Osc :
                       byte == '_' ? Kind::Apc :
                                     Kind::Dcs);
                // SOS and PM are swallowed as unsupported strings.
                if (byte != ']' && byte != 'P' && byte != '_')
                {
                    _discard();
                }
                continue;
            }
        }
        if (value >= 0x20 && value <= 0x2f)
        {
            _sequence.intermediates.append(byte);
            continue;
        }
        if (_state != State::Escape && value >= 0x30 && value <= 0x3f && _sequence.intermediates.isEmpty())
        {
            if (value >= 0x3c)
            {
                if (!_parameters.isEmpty())
                {
                    _discard();
                    _state = State::Ignore;
                }
                else
                {
                    _sequence.privateMarkers.append(byte);
                }
            }
            else
            {
                _parameters.append(byte);
            }
            continue;
        }
        const bool final = _state == State::Escape ? value >= 0x30 && value <= 0x7e : value >= 0x40 && value <= 0x7e;
        if (final)
        {
            _sequence.command = byte;
            if (!_parseParameters())
            {
                _discard();
            }
            if (_state == State::DcsHeader)
            {
                _state = State::String;
            }
            else
            {
                _emit(handler);
            }
        }
        else
        {
            _discard();
            _state = State::Ignore;
        }
    }
    flush();
}

void VtParser::reset()
{
    _abort();
    _discarded = 0;
}

void VtParser::setEightBitControls(bool enabled)
{
    _eightBitControls = enabled;
}

qsizetype VtParser::incompleteSequenceBytes() const
{
    return _sequence.data.size() + _sequence.intermediates.size() + _sequence.privateMarkers.size()
         + _parameters.size();
}

quint64 VtParser::discardedSequences() const
{
    return _discarded;
}

void VtParser::_start(State state, Kind kind)
{
    _abort();
    _state         = state;
    _sequence.kind = kind;
}

void VtParser::_abort()
{
    _state    = State::Ground;
    _sequence = {};
    _parameters.clear();
    _headerBytes = 0;
    _overflow    = false;
}

void VtParser::_discard()
{
    if (!_overflow)
    {
        ++_discarded;
    }
    _overflow = true;
    _sequence.data.clear();
    _sequence.parameters.clear();
    _sequence.intermediates.clear();
    _sequence.privateMarkers.clear();
    _parameters.clear();
}

bool VtParser::_parseParameters()
{
    if (_parameters.isEmpty())
    {
        return true;
    }
    const auto groups = _parameters.split(';');
    if (groups.size() > 32)
    {
        return false;
    }
    for (const auto& group : groups)
    {
        const auto parts = group.split(':');
        if (parts.size() > 16)
        {
            return false;
        }
        QVector<int> values;
        for (const auto& part : parts)
        {
            if (part.isEmpty())
            {
                values.append(-1);
                continue;
            }
            int value = 0;
            for (const char digit : part)
            {
                if (digit < '0' || digit > '9' || value > (std::numeric_limits<int>::max() - (digit - '0')) / 10)
                {
                    return false;
                }
                value = value * 10 + digit - '0';
            }
            values.append(value);
        }
        _sequence.parameters.append(values);
    }
    return true;
}

void VtParser::_emit(const Handler& handler)
{
    if (!_overflow)
    {
        handler(_sequence);
    }
    _abort();
}

void VtParser::_appendString(char byte)
{
    if (_overflow)
    {
        return;
    }
    const auto limit = _sequence.kind == Kind::Osc ? _oscLimit : _dcsLimit;
    if (_sequence.data.size() >= limit)
    {
        _discard();
    }
    else
    {
        _sequence.data.append(byte);
    }
}
