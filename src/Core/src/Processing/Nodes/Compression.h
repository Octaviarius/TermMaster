#pragma once

#include "Codec.h"

/** Owns the common and directional Compression parameters and its three fixed-topology creation choices. */
class Compression final
{
public:
    static Codec::Definition descriptor();
};
