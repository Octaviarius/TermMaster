#pragma once

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

class RandomNames
{
public:
    RandomNames();

    /** Generates an adjective/noun name without constructing the indexed permutation. */
    static PrettyId generate();

    PrettyId getId(int index);

    const QStringList nouns() const;
    const QStringList adjectives() const;

    int totalCombinations() const;

private:
    QList<int> _indices;
};
