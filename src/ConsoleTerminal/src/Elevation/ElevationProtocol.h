#pragma once

#include <QByteArray>
#include <QString>

/** Bounded byte framing shared by the unelevated client and one-shot console host. */
class ConsoleHostProtocol
{
public:
    enum class Type : quint8
    {
        Hello   = 1,
        Start   = 2,
        Ready   = 3,
        Input   = 4,
        Output  = 5,
        Resize  = 6,
        Written = 7,
        Exit    = 8,
        Failure = 9,
        Close   = 10,
        Closed  = 11
    };
    static constexpr qsizetype MaxFrameSize = 8 * 1024 * 1024;
    static QByteArray          frame(Type type, const QByteArray& payload = {});
    /** False with an empty error means incomplete input; malformed framing reports an error. */
    static bool take(QByteArray& buffer, Type& type, QByteArray& payload, QString* error = nullptr);
};
