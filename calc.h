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
 * calc.h - header for calc.c
 *
 * Calc performs FFTs for the whole file, calling the "notify" function
 * for each one it completes.
 * The result data is the linear FFT magnitudes from 0Hz to Nyquist frequency.
 */

#ifndef CALC_H

#if ECORE_MAIN
#include <Ecore.h>
#endif

#include "audio_file.h"
#include "spettro.h"
#include "window.h"

/* The parameters for a calculation, passed to calc(), giving all parameters,
 * also, an element of the list of needed FFTs used by the scheduler.
 *
 * The is also the result of an FFT, when it comes back from an FFT thread.
 */
typedef struct calc_t {
    /* These items define what calculation is to be or was performed */
    double		t;	/* FFT centered on when? */
    double		fft_freq; /* FFT frequency when scheduled */
    window_function_t	window;
    audio_file_t	*af;

    /* This is the result */
    float *		spec;	 /* The linear spectrum from [0..speclen]
    				  * for 0Hz to audio_file->sample_rate / 2 */
    /* Other data */
#if ECORE_MAIN
    Ecore_Thread *	thread;
#endif
    struct calc_t *	next;	/* List of calcs to perform, in time order */
} calc_t;

/* Used in recall_result() to see if the cache has any results for a column */
#define ANY_FFTFREQ (0)

extern void calc(calc_t *data);

/* How many columns to precalculate off the left and right edges of the screen.
 * A tenth of a screen width make normal operation seamless and
 * right-arrow-key motions immediate.
 * A screen's width (to make Shift Left&Right immediate) makes it sluggish.
 */
#define LOOKAHEAD ((max_x - min_x + 9) / 10)

#define CALC_H
#endif
