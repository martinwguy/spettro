/*
 * calc.c - Do all the heavy calculation of spectra.
 *
 * This file's only entry point is as the "func_heavy" parameter to
 * ecore_thread_feedback_run()
 *
 * The data points to a structure containing are all it needs to know
 * to calculate the complete spectrogram of the audio.
 *
 * The callback function signals a completed column of spectral data
 * which is coloured and its axis made logarithmic in the main loop
 * so as not to have to recalculate the FFT for zooms, pans and recolouring.
 */

#include <stdlib.h>
#include <math.h>
#include <fftw3.h>

#include <Ecore.h>

#include "spettro.h"
#include "audiofile.h"
#include "calc.h"
#include "spectrum.h"

/*
 * The compute-FFTs function
 *
 * Results are returned in a result_t (struct result) which is obtained from
 * malloc, as is the "spec" field of it.
 */

/* Helper function */
static result_t *get_result(calc_t *calc, spectrum *spec, double t);

void
calc(calc_t *calc, void (*result_cb)(result_t *))
{
    /* The real function parameters */
    double sr	  = calc->sr;		/* audio sample rate */
    double length = calc->length;	/* length of whole piece in seconds */
    double from   = calc->from;		/* centre of first FFT bucket */
    double to	  = calc->to;		/* centre of last FFT bucket */
    int	   speclen= calc->speclen;	/* Max index into result->spec */

    /* Variables */
    double step	  = 1 / calc->ppsec;
    int	   fftsize;			/* == speclen * 2 */
    spectrum *spec;
    double  t;				/* Time from start of piece */

    fftsize = speclen * 2;	/* Not sure that an odd fftsize would work */

    spec = create_spectrum(speclen, calc->window);
    if (spec == NULL) {
	fprintf(stderr, "Can't create spectrum.\n");
	return;
    }

    /* Ascending ranges or a single point */
    if (from <= to + DELTA)
	for (t = from; t <= to + DELTA; t += step)
	    (*result_cb)(get_result(calc, spec, t));

    /* Descending ranges */
    if (from > to + DELTA)
	for (t = from; t >= to - DELTA; t -= step)
	    (*result_cb)(get_result(calc, spec, t));

    destroy_spectrum(spec);
}

static result_t *
get_result(calc_t *calc, spectrum *spec, double t)
{
        result_t *result;	/* The result structure */
	int fftsize = calc->speclen * 2;

	result = (result_t *) malloc(sizeof(result_t));
	if (!result) {
	    fprintf(stderr, "Out of memory in calc()\n");
	    return;
	}

	result->t = t;
	result->speclen = calc->speclen;
	result->thread = calc->thread;

	/* Fetch the appropriate audio for our FFT source */
	/* The data is centred on the requested time. */
	if (!lock_audiofile()) {
	    fprintf(stderr, "Cannot lock audio file\n");
	    exit(1);
	}
	read_mono_audio_double(calc->audio_file, spec->time_domain,
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
	spec->mag_spec = calloc(calc->speclen + 1, sizeof(*(spec->mag_spec)));
	if (spec->mag_spec == NULL) {
	    fprintf(stderr, "Out of memory in calc()\n");
	    return;
	}

	/* Mark the converted data as not having been calculated yet */
	result->mag = NULL;

	return(result);
}
