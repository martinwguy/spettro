/*
 * spettro.h - definitions and declarations for everybody
 */

#ifndef SPETTRO_H

#include "configure.h"
#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include "alloc.h"

typedef int bool;

#ifndef FALSE
# define FALSE 0
#endif
#ifndef TRUE
# define TRUE 1
#endif

/* Slop factor for comparisons involving calculated floating point values. */
#define DELTA (1.0e-10)
#define DELTA_GT(a, b) ((a) > (b) + DELTA)
#define DELTA_LT(a, b) ((a) < (b) - DELTA)
#define DELTA_GE(a, b) ((a) >= (b) - DELTA)
#define DELTA_LE(a, b) ((a) <= (b) + DELTA)
#define DELTA_EQ(a, b) ((a) > (b) - DELTA && (a) < (b) + DELTA)
#define DELTA_NE(a, b) ((a) < (b) - DELTA || (a) > (b) + DELTA)

#define SPETTRO_H
#endif
