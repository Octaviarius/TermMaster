#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

/** Read-only device metadata classification; USB transport does not establish a UART interface. */
class PortClassification
{
public:
    enum class Bus
    {
        Unknown,
        Usb,
        Pci,
        Legacy,
        Bluetooth,
        Other
    };
    enum class Origin
    {
        Unknown,
        Hardware,
        Software
    };
    struct Node
    {
        QString     instanceId;
        QString     enumerator;
        QString     service;
        QString     busTypeGuid;
        QStringList hardwareIds;
    };
    struct Result
    {
        Bus         bus    = Bus::Unknown;
        Origin      origin = Origin::Unknown;
        QString     evidence;
        QString     instanceId;
        QString     service;
        QStringList hardwareIds;
        QString     label() const;
    };

    static Result classify(const QList<Node>& leafToRoot);
    static Result inspect(const QString& portName);
    /** Keys are uppercase port names; performs one device inventory scan. */
    static QMap<QString, Result> inspectMany(const QStringList& portNames);
};
