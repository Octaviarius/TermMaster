#pragma once

#include <QVariantMap>

/** Validated packet-length layout shared by the independent wrap and unwrap nodes. */
class LengthField final
{
public:
    explicit LengthField(const QVariantMap& configuration);

    static QByteArray schema();
    static bool       validate(QVariantMap& configuration, QString* error);

    int        width() const;
    bool       prefix() const;
    bool       value(qsizetype payloadBytes, quint64& result, QString& error) const;
    QByteArray encode(quint64 value) const;
    quint64    decode(const char* bytes) const;

private:
    int  _width;
    int  _adjustment;
    bool _prefix;
    bool _littleEndian;
    bool _includeField;
};
