#pragma once

#include <QFlag>
#include <stdint.h>

/**
 * @brief The TermAttribute class
 */
class TermAttribute
{
public:
    enum class Attribute : uint16_t
    {
        None = 0,

        Bold      = (1 << 0),
        Faint     = (1 << 1),
        Italic    = (1 << 2),
        Underline = (1 << 3),
        Strikeout = (1 << 4),
        Blink     = (1 << 5),
        FastBlink = (1 << 6),
        Inversed  = (1 << 7),
        Hidden    = (1 << 8),

        Foreground = (1 << 9),
        Background = (1 << 10),

        All = (1 << 10) - 1,
    };
    Q_DECLARE_FLAGS(Attributes, Attribute)

    uint16_t attrs      = 0;
    uint8_t  foreground = 0;
    uint8_t  background = 0;

    constexpr void setAttrs(Attributes flags)
    {
        attrs |= flags;
    }

    constexpr void clrAttrs(Attributes flags)
    {
        attrs &= ~flags;
    }

    constexpr bool operator==(const TermAttribute& other) const
    {
        return other.foreground == foreground && other.background == background && other.attrs == attrs;
    }

    constexpr bool operator!=(const TermAttribute& other) const
    {
        return !(other == *this);
    }

    constexpr bool isBold() const
    {
        return attrs & (uint16_t)Attribute::Bold;
    }

    constexpr bool isFaint() const
    {
        return attrs & (uint16_t)Attribute::Faint;
    }

    constexpr bool isItalic() const
    {
        return attrs & (uint16_t)Attribute::Italic;
    }

    constexpr bool isUnderline() const
    {
        return attrs & (uint16_t)Attribute::Underline;
    }

    constexpr bool isStrikeout() const
    {
        return attrs & (uint16_t)Attribute::Strikeout;
    }

    constexpr bool isHidden() const
    {
        return attrs & (uint16_t)Attribute::Hidden;
    }

    constexpr bool isInversed() const
    {
        return attrs & (uint16_t)Attribute::Inversed;
    }

    constexpr bool isBlink() const
    {
        return attrs & (uint16_t)Attribute::Blink;
    }

    constexpr bool isFastBlink() const
    {
        return attrs & (uint16_t)Attribute::FastBlink;
    }

    constexpr bool hasBackground() const
    {
        return attrs & (uint16_t)Attribute::Background;
    }
    constexpr bool hasForeground() const
    {
        return attrs & (uint16_t)Attribute::Foreground;
    }

    constexpr bool hasNothing() const
    {
        return attrs == (uint16_t)Attribute::None;
    }
};
Q_DECLARE_OPERATORS_FOR_FLAGS(TermAttribute::Attributes);

/**
 * @brief The TermChar class
 */
class TermChar
{
public:
    uint32_t      symbol;
    TermAttribute attrs;

    TermChar() : symbol(0), attrs {0, 0, 0}
    {
    }

    TermChar(uint32_t symbol) : symbol(symbol), attrs {0, 0, 0}
    {
    }

    TermChar(uint32_t symbol, TermAttribute attrs) : symbol(symbol), attrs(attrs)
    {
    }

    void set(uint32_t symbol, TermAttribute attrs)
    {
        this->symbol = symbol;
        this->attrs  = attrs;
    }

    void reset()
    {
        *this = zeroFiller;
    }

    static const TermChar zeroFiller;
};
