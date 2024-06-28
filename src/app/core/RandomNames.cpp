#include "RandomNames.h"

#include "SettingsManager.h"

#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>

RandomNames::RandomNames()
{
    auto conf = SettingsManager::instance().generalSettings("random_names");

    QFile nouns(conf.value("nouns_file", "nouns.lst").toString());
    QFile adj(conf.value("adjectives_file", "adjectives.lst").toString());

    if (nouns.open(QFile::ReadOnly))
    {
        _nouns = QString(nouns.readAll()).split(QRegularExpression("[\r\n]+"));
        nouns.close();
    }

    if (adj.open(QFile::ReadOnly))
    {
        _adjectives = QString(adj.readAll()).split(QRegularExpression("[\r\n]+"));
        adj.close();
    }

    int nl = _nouns.length();
    int al = _adjectives.length();
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
    int nounIndex      = shuffledIndex % _nouns.size();
    int adjectiveIndex = shuffledIndex / _nouns.size();

    return PrettyId(_nouns[nounIndex], _adjectives[adjectiveIndex]);
}

const QStringList RandomNames::nouns() const
{
    return _nouns;
}

const QStringList RandomNames::adjectives() const
{
    return _adjectives;
}

int RandomNames::totalCombinations() const
{
    return _nouns.length() * _adjectives.length();
}
