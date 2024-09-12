#include "RandomNames.h"

#include "core/randomNamesData.h"

#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>

RandomNames::RandomNames()
{
    int nl = nounsList.length();
    int al = adjectivesList.length();
    int tl = totalCombinations();

    qDebug() << QString("RandomNames: %1 nouns, %2 adjectives, %3 total combinations").arg(nl).arg(al).arg(tl);

    auto gen = QRandomGenerator();

    _indices.resize(tl);

    for (int i = 0; i < tl; i++)
    {
        _indices[i] = i;
    }

    for (int i = tl - 1; i > 0; i--)
    {
        int j = gen.bounded(i + 1);
        std::swap(_indices[i], _indices[j]);
    }
}

PrettyId RandomNames::getId(int index)
{
    int shuffledIndex  = _indices[index];
    int nounIndex      = shuffledIndex % nounsList.size();
    int adjectiveIndex = shuffledIndex / nounsList.size();

    return PrettyId(nounsList[nounIndex], adjectivesList[adjectiveIndex]);
}

const QStringList RandomNames::nouns() const
{
    return nounsList;
}

const QStringList RandomNames::adjectives() const
{
    return adjectivesList;
}

int RandomNames::totalCombinations() const
{
    return nounsList.length() * adjectivesList.length();
}
