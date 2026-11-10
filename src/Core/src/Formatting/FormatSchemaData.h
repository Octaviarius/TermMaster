#pragma once

#include <Termaster/Core/Formatting/FormatSchema.h>
#include <atomic>

struct FormatSchema::Data
{
    struct Entry
    {
        QString name;
        Kind    kind;
    };

    std::vector<Entry> entries;
    std::atomic_bool   sealed = false;
};
