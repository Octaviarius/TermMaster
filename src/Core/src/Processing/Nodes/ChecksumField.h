#pragma once

#include <QByteArrayView>
#include <QVariantMap>
#include <array>

/** Immutable whole-packet checksum layout and instance-owned byte lookup table. */
class ChecksumField final
{
public:
    explicit ChecksumField(const QVariantMap& configuration);

    static QByteArray schema();
    static bool       validate(QVariantMap& configuration, QString* error);

    int        width() const;
    bool       prefix() const;
    QByteArray encode(QByteArrayView payload) const;

private:
    enum class Algorithm
    {
        Sum,
        Xor,
        Crc
    };
    struct Parameters
    {
        int     width      = 16;
        quint32 polynomial = 0x8005;
        quint32 init       = 0xffff;
        bool    refin      = true;
        bool    refout     = true;
        quint32 xorout     = 0;
    };

    Algorithm                _algorithm;
    Parameters               _parameters;
    quint32                  _mask;
    bool                     _prefix;
    bool                     _littleEndian;
    std::array<quint32, 256> _table {};

    static Parameters _parametersFor(const QVariantMap& configuration);
    static quint32    _reflect(quint32 value, int width);
    quint32           _checksum(QByteArrayView payload) const;
};
