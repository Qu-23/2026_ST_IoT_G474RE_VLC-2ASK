/**
  ******************************************************************************
  * @file    Font.h
  * @brief   Font data declarations (extern only)
  ******************************************************************************
  */

#ifndef __FONT_H
#define __FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Font count macros - 16x16 Chinese font enabled for demo */
#define hz16_num 31
#define hz24_num 0

/* Font data declarations - defined in Font.c */
extern const unsigned char asc16[];
extern const unsigned char sz32[];

struct typFNT_GB162 {
    unsigned char Index[2];
    char Msk[32];
};

struct typFNT_GB242 {
    unsigned char Index[2];
    char Msk[72];
};

extern const struct typFNT_GB162 hz16[];
extern const struct typFNT_GB242 hz24[];

#ifdef __cplusplus
}
#endif

#endif /* __FONT_H */