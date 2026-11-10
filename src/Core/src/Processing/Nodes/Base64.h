#pragma once

#include "Codec.h"

/** Owns the common and directional Base64 parameters and its three fixed-topology creation choices. */
class Base64 final
{
public:
    static Codec::Definition descriptor();
};
