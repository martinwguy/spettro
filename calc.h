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
    double		length;	/* Length of piece in seconds */
    double		sr;	/* Sample rate of the piece */
    double		t;	/* FFT centred on  when? */
    double		ppsec;	/* Pixel columns per second */
    int			speclen; /* Size of spectrum == fftsize/2 */
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
    double t;		/* An FFT centred on what time in the piece? */
    int speclen;	/* The length of the required linear spectrum */
    window_function_t window; /* Apply which window function to the audio data? */
    float *spec;	/* The linear spectrum from [0..speclen] = 0Hz..sr/2 */
    int maglen;		/* Length of magnitude data on log axis */
    float *logmag;	/* log10 of magnitude data from [0..maglen-1] */
#if ECORE_MAIN
    Ecore_Thread *	thread;
#endif
    struct result *next; /* Linked list of results in time order */
} result_t;

extern void calc(calc_t *data);

#define CALC_H
#endif
