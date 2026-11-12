#include <QRandomGenerator>
#include <Termaster/Core/WordGenerator.h>
#include <array>
#include <span>
#include <stdexcept>
#include <string_view>

namespace
{
using Choices = std::span<const std::string_view>;

constexpr std::array<std::string_view, 32> starts {"b",  "br", "bl", "c",  "cl", "cr", "d",  "dr", "f",  "fl", "fr",
                                                   "g",  "gl", "gr", "h",  "j",  "k",  "l",  "m",  "n",  "p",  "pl",
                                                   "pr", "r",  "s",  "sh", "sk", "sl", "st", "t",  "th", "tr"};
constexpr std::array<std::string_view, 5>  vowels {"a", "e", "i", "o", "u"};
constexpr std::array<std::string_view, 16>
    middles {"b", "d", "f", "g", "k", "l", "m", "n", "p", "r", "s", "t", "th", "v", "w", "z"};
constexpr std::array<std::string_view, 6> endings {"n", "r", "s", "th", "l", "m"};

constexpr std::array<Choices, 5> twoSyllableSlots {starts, vowels, middles, vowels, endings};
constexpr std::array<Choices, 7> threeSyllableSlots {starts, vowels, middles, vowels, middles, vowels, endings};

struct Block
{
    quint32                  offset;
    quint32                  size;
    quint32                  inverse;
    std::span<const Choices> positions;
};

// Prototype block sizes and modular inverses of 16127 for its fixed default lexicon.
constexpr std::array blocks {
    Block {    0,   76800,   17663,   twoSyllableSlots},
    Block {76800, 6144000, 1016063, threeSyllableSlots},
};
constexpr quint32 multiplier    = 16127;
constexpr quint32 shift         = 7919;
constexpr quint32 totalCapacity = blocks.back().offset + blocks.back().size;
static_assert((quint64(multiplier) * blocks[0].inverse) % blocks[0].size == 1);
static_assert((quint64(multiplier) * blocks[1].inverse) % blocks[1].size == 1);

bool _isVowel(QChar character)
{
    return character == u'a' || character == u'e' || character == u'i' || character == u'o' || character == u'u';
}
} // namespace

quint32 WordGenerator::capacity()
{
    return totalCapacity;
}

QString WordGenerator::word(quint32 id)
{
    if (id >= capacity())
    {
        throw std::out_of_range("WordGenerator ID exceeds capacity");
    }

    const auto&                     block = id < blocks[1].offset ? blocks[0] : blocks[1];
    quint64                         value = (quint64(id - block.offset) * multiplier + shift) % block.size;
    std::array<std::string_view, 7> tokens;
    for (std::size_t index = block.positions.size(); index > 0; --index)
    {
        const auto choices  = block.positions[index - 1];
        tokens[index - 1]   = choices[value % choices.size()];
        value              /= choices.size();
    }

    QString result;
    result.reserve(11);
    for (std::size_t index = 0; index < block.positions.size(); ++index)
    {
        const auto token  = tokens[index];
        result           += QLatin1StringView(token.data(), static_cast<qsizetype>(token.size()));
    }
    return result;
}

std::optional<quint32> WordGenerator::number(QStringView word)
{
    // Seven tokens and eleven letters bound the default three-syllable spelling.
    if (word.isEmpty() || word.size() > 11)
    {
        return std::nullopt;
    }
    for (const auto character : word)
    {
        if (character < u'a' || character > u'z')
        {
            return std::nullopt;
        }
    }

    std::array<QStringView, 7> tokens;
    std::size_t                count = 0;
    for (qsizetype position = 0; position < word.size();)
    {
        if (count == tokens.size())
        {
            return std::nullopt;
        }
        const auto start = position++;
        if (!_isVowel(word[start]))
        {
            while (position < word.size() && !_isVowel(word[position]))
            {
                ++position;
            }
        }
        tokens[count++] = word.mid(start, position - start);
    }

    for (const auto& block : blocks)
    {
        if (count != block.positions.size())
        {
            continue;
        }
        quint32 value = 0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto  choices = block.positions[index];
            std::size_t choice  = 0;
            while (choice < choices.size()
                   && tokens[index]
                          != QLatin1StringView(choices[choice].data(), static_cast<qsizetype>(choices[choice].size())))
            {
                ++choice;
            }
            if (choice == choices.size())
            {
                return std::nullopt;
            }
            value = value * static_cast<quint32>(choices.size()) + static_cast<quint32>(choice);
        }

        const quint64 unshifted = (value + block.size - shift) % block.size;
        return block.offset + static_cast<quint32>((unshifted * block.inverse) % block.size);
    }
    return std::nullopt;
}

QString WordGenerator::generate()
{
    auto result = word(QRandomGenerator::global()->bounded(capacity()));
    result[0]   = result[0].toUpper();
    return result;
}
