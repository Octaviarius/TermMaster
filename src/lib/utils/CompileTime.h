#ifndef COMPILETIME_H
#define COMPILETIME_H

#include <stdint.h>

#define COMPILETIME_YEAR                                                                                               \
    ((uint16_t)(__DATE__[7] - '0') * 1000 + (uint16_t)(__DATE__[8] - '0') * 100 + (uint16_t)(__DATE__[9] - '0') * 10   \
     + (uint16_t)(__DATE__[10] - '0') * 1)

#define COMPILETIME_MONTH                                                                                              \
    (__DATE__[0] == 'J' && __DATE__[1] == 'a'                       ? (uint8_t)1 :                                     \
     __DATE__[0] == 'F'                                             ? (uint8_t)2 :                                     \
     __DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'r' ? (uint8_t)3 :                                     \
     __DATE__[0] == 'A' && __DATE__[1] == 'p'                       ? (uint8_t)4 :                                     \
     __DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'y' ? (uint8_t)5 :                                     \
     __DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'n' ? (uint8_t)6 :                                     \
     __DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'l' ? (uint8_t)7 :                                     \
     __DATE__[0] == 'A' && __DATE__[1] == 'u'                       ? (uint8_t)8 :                                     \
     __DATE__[0] == 'S'                                             ? (uint8_t)9 :                                     \
     __DATE__[0] == 'O'                                             ? (uint8_t)10 :                                    \
     __DATE__[0] == 'N'                                             ? (uint8_t)11 :                                    \
     __DATE__[0] == 'D'                                             ? (uint8_t)12 :                                    \
                                                                      (uint8_t)0)

#define COMPILETIME_DAY                                                                                                \
    ((uint8_t)((__DATE__[4] == ' ' ? '0' : __DATE__[4]) - '0') * 10 + (uint8_t)(__DATE__[5] - '0') * 1)

#define COMPILETIME_HOUR ((uint8_t)(__TIME__[0] - '0') * 10 + (uint8_t)(__TIME__[1] - '0') * 1)

#define COMPILETIME_MIN  ((uint8_t)(__TIME__[3] - '0') * 10 + (uint8_t)(__TIME__[4] - '0') * 1)

#define COMPILETIME_SEC  ((uint8_t)(__TIME__[6] - '0') * 10 + (uint8_t)(__TIME__[7] - '0') * 1)

#endif // COMPILETIME_H
