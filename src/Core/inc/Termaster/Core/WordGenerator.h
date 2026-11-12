#pragma once

#include <QString>
#include <QStringView>
#include <optional>

/** Reversible two- and three-syllable names using the default WordGenerator prototype lexicon. */
class WordGenerator
{
public:
    static quint32 capacity();

    /** Returns the lowercase spelling; throws std::out_of_range for an invalid ID. */
    static QString word(quint32 id);
    /** Returns an ID only for an exact lowercase spelling in the fixed lexicon. */
    static std::optional<quint32> number(QStringView word);

    /** Uniformly samples the name space and capitalizes the first letter. Names may repeat. */
    static QString generate();
};
