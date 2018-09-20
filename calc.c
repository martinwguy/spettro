/*
 * calc.c - Do all the heavy calculation of spectra.
 *
 * This file's only entry point is calc()
 *
 * The data points to a structure containing are all it needs to know
 * to calculate the complete spectrogram of the audio.
 *
 * The callback function signals a completed column of spectral data
 * which is colored and its axis made logarithmic in the main loop
 * so as not to have to recalculate the FFT for zooms, pans and recoloring.
 */

#include "config.h"

#include <stdlib.h>
#include <math.h>
#include <fftw3.h>

#if ECORE_MAIN
#include <Ecore.h>
#endif

#include "spettro.h"
#include "audio_file.h"
#include "calc.h"
#include "lock.h"
#include "spectrum.h"
#include "main.h"	/* for calc_result() */

/*
 * The compute-FFTs function
 *
 * Results are returned in a result_t (struct result) which is obtained from
 * malloc, as is the "spec" field of it to calc_result() in main.c.
 * Passing a constant callback address down the call chain is just too awful.
 */

/* Helper function */
static result_t *get_result(calc_t *calc, spectrum *spec, double t);

void
calc(calc_t *calc)
{
    /* The real function parameters */
    double from   = calc->from;		/* centre of first FFT bucket */
    double to	  = calc->to;		/* centre of last FFT bucket;
					 * If == 0.0, just do "from" */
    int	   speclen= calc->speclen;	/* Max index into result->spec */

    /* Variables */
    double step	  = 1 / calc->ppsec;
    spectrum *spec;
    double  t;				/* Time from start of piece */

    spec = create_spectrum(speclen, calc->window);
    if (spec == NULL) {
	fprintf(stderr, "Can't create spectrum.\n");
	return;
    }

    /* Ascending ranges or a single point.
     * Also handles to == 0.0, which is just "from".
     */
    if (to == 0.0 || from <= to + DELTA) {
	t = from; 
	do {
	    result_t *result = get_result(calc, spec, t);
#if ECORE_MAIN
	    /* Don't return a result if our thread has a cancel request */
	    if (ecore_thread_check(result->thread) == FALSE)
#endif
	    calc_result(result);
	} while ((t += step) <= to + DELTA);
    }

    /* Descending ranges */
    if (to != 0.0 && from > to + DELTA)
	for (t = from; t >= to - DELTA; t -= step) {
	    result_t *result = get_result(calc, spec, t);
#if ECORE_MAIN
	    /* Don't return a result if our thread has a cancel request */
	    if (ecore_thread_check(result->thread) == FALSE)
#endif
	    calc_result(result);
    }

    destroy_spectrum(spec);
}

/*
 * Calculate the magnitude spectrum for a column
 */
static result_t *
get_result(calc_t *calc, spectrum *spec, double t)
{
        result_t *result;	/* The result structure */
	int fftsize = calc->speclen * 2;

	result = (result_t *) malloc(sizeof(result_t));
	if (!result) {
	    fprintf(stderr, "Out of memory in calc()\n");
	    return NULL;
	}

	result->t = t;
	result->speclen = calc->speclen;
#if ECORE_MAIN
	result->thread = calc->thread;
#endif

	/* Fetch the appropriate audio for our FFT source */
	/* The data is centred on the requested time. */
	if (!lock_audiofile()) {
	    fprintf(stderr, "Cannot lock audio file\n");
	    exit(1);
	}
	read_audio_file(calc->audio_file, (char *) spec->time_domain,
			af_double, 1,
			lrint(t * calc->sr) - fftsize/2, fftsize);
	if (!unlock_audiofile()) {
	    fprintf(stderr, "Cannot unlock audio file\n");
	    exit(1);
	}

	calc_magnitude_spectrum(spec);

	/* We need to pass back a buffer obtained from malloc() that will
	 * subsequently be freed or kept. Rather than memcpy() it, we hijack
	 * the already-allocated buffer and malloc a new one for next time.
	 */
	result->spec = spec->mag_spec;
	spec->mag_spec = malloc((calc->speclen+1) * sizeof(*(spec->mag_spec)));
	if (spec->mag_spec == NULL) {
	    fprintf(stderr, "Out of memory in calc()\n");
	    return NULL;
	}

	/* Mark the converted data as not having been calculated yet */
	result->mag = NULL;

	return(result);
}
