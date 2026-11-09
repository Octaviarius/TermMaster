#include "ElevationProtocol.h"

#include <QtEndian>

QByteArray ConsoleHostProtocol::frame(Type type, const QByteArray& payload)
{
    if (payload.size() > MaxFrameSize - 1)
    {
        return {};
    }
    QByteArray result(5, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(payload.size() + 1), result.data());
    result[4] = static_cast<char>(type);
    result.append(payload);
    return result;
}

bool ConsoleHostProtocol::take(QByteArray& buffer, Type& type, QByteArray& payload, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (buffer.size() < 4)
    {
        return false;
    }
    const auto size = qFromBigEndian<quint32>(buffer.constData());
    if (size < 1 || size > MaxFrameSize)
    {
        if (error)
        {
            *error = QStringLiteral("Invalid console host frame length");
        }
        return false;
    }
    if (buffer.size() < static_cast<qsizetype>(size) + 4)
    {
        return false;
    }
    const auto value = static_cast<quint8>(buffer[4]);
    if (value < static_cast<quint8>(Type::Hello) || value > static_cast<quint8>(Type::Closed))
    {
        if (error)
        {
            *error = QStringLiteral("Unknown console host frame type");
        }
        return false;
    }
    type    = static_cast<Type>(value);
    payload = buffer.mid(5, size - 1);
    buffer.remove(0, size + 4);
    return true;
}
