"""Deterministic, pronounceable name prototype: 25,600 distinct lowercase words.

Invented spellings may coincide with real words or names. No dictionary filtering.
"""

import argparse
import math


STARTS = "b br bl c cl cr d dr f fl fr g gl gr h j k l m n p pl pr r s sh sk sl st t th tr".split()
VOWELS = "aeiou"
MIDDLES = "b d f g k l m n p r s t th v w z".split()
ENDINGS = "nr"
CAPACITY = len(STARTS) * len(VOWELS) * len(MIDDLES) * len(VOWELS) * len(ENDINGS)


def word(number: int) -> str:
    """Map an integer in [0, CAPACITY) to a unique pseudo-English spelling."""
    if not isinstance(number, int) or isinstance(number, bool):
        raise TypeError("number must be an integer")
    if not 0 <= number < CAPACITY:
        raise ValueError(f"number must be between 0 and {CAPACITY - 1}")

    # A bijective shuffle: neighboring numbers produce less similar names.
    value = (number * 16127 + 7919) % CAPACITY
    parts = []
    for choices in (ENDINGS, VOWELS, MIDDLES, VOWELS, STARTS):
        value, index = divmod(value, len(choices))
        parts.append(choices[index])
    return "".join(reversed(parts))


def check() -> None:
    """Exhaustively verify capacity, uniqueness and output constraints."""
    names = [word(number) for number in range(CAPACITY)]
    if math.gcd(16127, CAPACITY) != 1 or len(set(names)) != CAPACITY:
        raise RuntimeError("The mapping is not bijective")
    if not all(name.isascii() and name.isalpha() and name.islower() for name in names):
        raise RuntimeError("Unexpected output characters")
    for invalid in (-1, CAPACITY):
        try:
            word(invalid)
        except ValueError:
            continue
        raise RuntimeError("Out-of-range input accepted")
    print(f"Verified {CAPACITY:,} unique words; lengths {min(map(len, names))}–{max(map(len, names))}.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("number", type=int, nargs="?", default=0)
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.count < 1 or args.number < 0 or args.number + args.count > CAPACITY:
        parser.error(f"requested range must fit within 0..{CAPACITY - 1}")
    if args.check:
        check()
    for number in range(args.number, args.number + args.count):
        print(f"{number:5d}  {word(number)}")


if __name__ == "__main__":
    main()
