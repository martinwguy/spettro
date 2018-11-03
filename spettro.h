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
#define DELTA (1.0e-6)

#define SPETTRO_H
#endif
