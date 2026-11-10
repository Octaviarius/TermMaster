#pragma once

#include <QVariantMap>

/** Validated fixed-pair byte mapping and whole-packet boundary policy. */
class StuffingSettings final
{
public:
    explicit StuffingSettings(const QVariantMap& configuration);

    static QByteArray schema();
    static bool       validate(QVariantMap& configuration, QString* error);

    unsigned char delimiter() const;
    unsigned char escape() const;
    unsigned char delimiterCode() const;
    unsigned char escapeCode() const;
    int           boundaries() const;

private:
    unsigned char _delimiter;
    unsigned char _escape;
    unsigned char _delimiterCode;
    unsigned char _escapeCode;
    int           _boundaries;
};
