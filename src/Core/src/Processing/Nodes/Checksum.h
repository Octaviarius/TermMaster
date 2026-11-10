#pragma once

#include "Codec.h"

/** Owns the common and directional Checksum parameters and its three fixed-topology creation choices. */
class Checksum final
{
public:
    static Codec::Definition descriptor();
};
