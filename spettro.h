/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * spettro.h - definitions and declarations for everybody
 */

#ifndef SPETTRO_H

#include "configure.h"
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "alloc.h"

typedef int bool;

#ifndef FALSE
# define FALSE 0
#endif
#ifndef TRUE
# define TRUE 1
#endif

#define MIN(a,b) ((a) < (b) ? (a) : (b))

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
