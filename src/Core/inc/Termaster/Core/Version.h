#pragma once

#include <QDateTime>
#include <QVersionNumber>

class Version
{
public:
    static const QVersionNumber versionNumber;

    static const QString gitRevision;
    static const QString gitBranch;

    static const QString   shortVersion;
    static const QString   longVersion;
    static const QDateTime buildTime;
};
