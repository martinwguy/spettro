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
 * calc.h - header for interface to calc.c
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

/* The parameters to calc(), saying what it should FFT.
 * Also, an element of the list of FFTs to perform used by the scheduler.
 */
typedef struct calc_t {
    audio_file_t *	audio_file; /* Our audio file handle */
    double		t;	/* FFT centred on  when? */
    double		fft_freq; /* FFT frequency when scheduled */
    window_function_t	window;
#if ECORE_MAIN
    Ecore_Thread *	thread;
#endif
    struct calc_t *	next;	/* List of calcs to perform, in time order */
    struct calc_t *	prev;	/* Reverse pointer of doubly-linked list */
} calc_t;

/*
 * Structure for results from calc() with the linear FFT for a pixel column,
 * callbacked to calc_result() as each column is ready.
 *
 * Fields logmag and maglen are not filled by calc(); the main routine calls
 * interpolate() on each one to generate columns of magnitudes for the screen,
 * as the mapping changes when they pan up and down or zoom the Y axis,
 * without needing to recalculate the FFT.
 */

typedef struct result {
    double		t;		/* An FFT centred on what time in the piece? */
    double		fft_freq;	/* The FFT frequency for this result */
    int			speclen;	/* The length of the required linear spectrum */
    window_function_t	window; 	/* Apply which window function to the audio data? */
    float *		spec;		/* The linear spectrum from [0..speclen]
    					 * representing 0Hz to audio_file->sample_rate / 2 */
    audio_file_t *	audio_file;	/* Which audio file this result came from */
#if ECORE_MAIN
    Ecore_Thread *	thread;
#endif
    struct result *	next; 		/* Linked list of results, in time order */
} result_t;

extern void calc(calc_t *data);

#define CALC_H
#endif
