#pragma once

#include "utils/Singleton.h"

#include <QPair>
#include <QStringList>

class PrettyId : public QPair<QString, QString>
{
public:
    using Pair = QPair<QString, QString>;

    PrettyId(QString name, QString adj) : Pair(name, adj)
    {
    }

    operator QString() const
    {
        return this->toString();
    }

    QString toString(bool isCapitals = true) const
    {
        if (isCapitals)
        {
            auto res                 = QString("%1 %2").arg(second).arg(first);
            res[0]                   = res[0].toUpper();
            res[second.length() + 1] = res[second.length() + 1].toUpper();
            return res;
        }
        else
        {
            return QString("%1 %2").arg(second).arg(first);
        }
    }
};

class RandomNames : public Singleton<RandomNames>
{
public:
    friend Singleton<RandomNames>;

    PrettyId getId(int index) const;

    const QStringList nouns() const;
    const QStringList adjectives() const;

    int totalCombinations() const;

private:
    RandomNames();

    QStringList _nouns;
    QStringList _adjectives;
    QList<int>  _indices;
};
