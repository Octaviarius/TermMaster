#pragma once

#include <QColor>
#include <array>

using TermColorMap = std::array<QColor, 256>;

extern const TermColorMap defaultTermColorMap;
