"""Reversible mixed-radix pseudo-English words; no random search or dictionary filter.

Extending max_syllables preserves existing IDs. Changing the lexicon changes names.
Invented spellings may coincide with real words or names. This is a standalone prototype.
"""

import argparse
import math
import re


STARTS = tuple("b br bl c cl cr d dr f fl fr g gl gr h j k l m n p pl pr r s sh sk sl st t th tr".split())
VOWELS = tuple("aeiou")
MIDDLES = tuple("b d f g k l m n p r s t th v w z".split())
ENDINGS = tuple("n r s th l m".split())


class WordGenerator:
    """Each vowel delimits a syllable, making the spelling uniquely decodable."""

    def __init__(self, min_syllables=2, max_syllables=3, *, starts=STARTS,
                 vowels=VOWELS, middles=MIDDLES, endings=ENDINGS, shuffle=True):
        if not 1 <= min_syllables <= max_syllables <= 12:
            raise ValueError("syllable range must fit within 1..12")
        self.starts = tuple(starts)
        self.vowels = tuple(vowels)
        self.middles = tuple(middles)
        self.endings = tuple(endings)
        self.shuffle = shuffle
        for choices in (self.starts, self.vowels, self.middles, self.endings):
            if not choices or len(set(choices)) != len(choices):
                raise ValueError("lexicons must be nonempty and contain unique tokens")
        if any(len(v) != 1 or v not in "aeiou" for v in self.vowels):
            raise ValueError("vowels must be single letters from aeiou")
        for token in self.starts + self.middles + self.endings:
            if not re.fullmatch("[bcdfghjklmnpqrstvwxyz]+", token):
                raise ValueError("consonant tokens must contain only lowercase ASCII consonants")
        self.blocks = []
        self.capacity = 0
        for syllables in range(min_syllables, max_syllables + 1):
            slots = (self.starts, self.vowels) + (self.middles, self.vowels) * (syllables - 1) + (self.endings,)
            size = math.prod(map(len, slots))
            multiplier = 16127
            while math.gcd(multiplier, size) != 1:
                multiplier += 2
            self.blocks.append((self.capacity, size, slots, multiplier))
            self.capacity += size

    def word(self, number):
        if not isinstance(number, int) or isinstance(number, bool):
            raise TypeError("number must be an integer")
        if not 0 <= number < self.capacity:
            raise ValueError(f"number must be in 0..{self.capacity - 1}")
        for offset, size, slots, multiplier in self.blocks:
            if number < offset + size:
                value = number - offset
                if self.shuffle:
                    value = (value * multiplier + 7919) % size
                parts = []
                # The remainder selects a lexeme; the quotient supplies the next choice.
                for choices in reversed(slots):
                    value, index = divmod(value, len(choices))
                    parts.append(choices[index])
                return "".join(reversed(parts))
        raise RuntimeError("unreachable")

    def number(self, word):
        if not isinstance(word, str) or not re.fullmatch("[a-z]+", word):
            raise ValueError("expected a lowercase word")
        tokens = re.findall("[bcdfghjklmnpqrstvwxyz]+|[aeiou]", word)
        for offset, size, slots, multiplier in self.blocks:
            if len(tokens) != len(slots):
                continue
            value = 0
            try:
                for token, choices in zip(tokens, slots):
                    value = value * len(choices) + choices.index(token)
            except ValueError:
                continue
            if self.shuffle:
                value = ((value - 7919) * pow(multiplier, -1, size)) % size
            return offset + value
        raise ValueError("word is outside this generator's lexicon")


def check(generator, count):
    seen = set()
    for number in range(min(count, generator.capacity)):
        name = generator.word(number)
        if name in seen or generator.number(name) != number:
            raise RuntimeError(f"collision or round-trip failure at {number}")
        seen.add(name)
    for offset, size, _, _ in generator.blocks:
        for number in (offset, offset + size - 1):
            if generator.number(generator.word(number)) != number:
                raise RuntimeError("block boundary failure")
    extended = WordGenerator(max_syllables=4)
    default = WordGenerator()
    for number in (0, 76799, default.capacity - 1):
        if default.word(number) != extended.word(number):
            raise RuntimeError("extending capacity changed an existing ID")
    print(f"Verified {len(seen):,} unique words, inverse mapping and block boundaries.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("number", type=int, nargs="?", default=0)
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--syllables", type=int, nargs=2, default=(2, 3), metavar=("MIN", "MAX"))
    parser.add_argument("--ordered", action="store_true", help="disable the reversible shuffle")
    parser.add_argument("--check", type=int, default=0, metavar="COUNT")
    args = parser.parse_args()
    try:
        generator = WordGenerator(*args.syllables, shuffle=not args.ordered)
        if args.count < 1 or args.number < 0 or args.number + args.count > generator.capacity or args.check < 0:
            raise ValueError("requested range is outside generator capacity")
    except ValueError as error:
        parser.error(str(error))
    print(f"Capacity: {generator.capacity:,}")
    if args.check:
        check(generator, args.check)
    for number in range(args.number, args.number + args.count):
        print(f"{number:10d}  {generator.word(number)}")


if __name__ == "__main__":
    main()
