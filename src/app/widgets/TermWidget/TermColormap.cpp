#include "TermColormap.h"

const TermColorMap defaultTermColorMap = ([]() {
    TermColorMap ret;

    int r, g, b;

    for (int colorIndex = 0; colorIndex < 256; colorIndex++)
    {
        if (colorIndex < 16)
        {
            // Системные цвета (0-15)
            static const int basicColors[16][3] = {
                {  0,   0,   0},
                {128,   0,   0},
                {  0, 128,   0},
                {128, 128,   0},
                {  0,   0, 128},
                {128,   0, 128},
                {  0, 128, 128},
                {192, 192, 192},
                {128, 128, 128},
                {255,   0,   0},
                {  0, 255,   0},
                {255, 255,   0},
                {  0,   0, 255},
                {255,   0, 255},
                {  0, 255, 255},
                {255, 255, 255}
            };

            r = basicColors[colorIndex][0];
            g = basicColors[colorIndex][1];
            b = basicColors[colorIndex][2];
        }
        else if (colorIndex >= 16 && colorIndex <= 231)
        {
            // Цвета 6x6x6 куба (16-231)
            int index      = colorIndex - 16;
            int redLevel   = index / 36;
            int greenLevel = (index / 6) % 6;
            int blueLevel  = index % 6;

            r = 55 * redLevel + 40;
            g = 55 * greenLevel + 40;
            b = 55 * blueLevel + 40;
        }
        else if (colorIndex >= 232 && colorIndex <= 255)
        {
            // Градации серого (232-255)
            int grayLevel = colorIndex - 232;
            r = g = b = 8 + 10 * grayLevel;
        }

        ret[colorIndex] = QColor(r, g, b);
    }

    return ret;
})();
