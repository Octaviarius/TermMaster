#pragma once

#include <QVariantMap>

/** Validated packet-length layout shared by the independent wrap and unwrap nodes. */
class LengthField final
{
public:
    explicit LengthField(const QVariantMap& configuration);

    static QByteArray schema();
    static bool       validate(QVariantMap& configuration, QString* error);

    bool       prefix() const;
    bool       value(qsizetype payloadBytes, quint64& result, int& width, QString& error) const;
    QByteArray encode(quint64 value) const;
    bool       decode(const QByteArray& packet, quint64& result, int& width, qsizetype maxSteps, QString& error) const;

private:
    enum class Encoding
    {
        Fixed,
        Varint
    };

    Encoding _encoding;
    int      _width;
    int      _maxBytes;
    int      _adjustment;
    bool     _prefix;
    bool     _littleEndian;
    bool     _includeField;

    bool       _value(qsizetype payloadBytes, int width, quint64& result) const;
    static int _varintWidth(quint64 value);
};
