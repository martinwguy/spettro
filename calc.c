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
void
calc(calc_t *calc, void (*result_cb)(result_t *))
{
    /* The real function parameters */
    double sr	  = calc->sr;		/* audio sample rate */
    double length = calc->length;	/* length of whole piece in seconds */
    double from   = calc->from;		/* centre of first FFT bucket */
    double to	  = calc->to;		/* centre of last FFT bucket */
    double ppsec  = calc->ppsec;	/* How many results per second? */
    int	   speclen= calc->speclen;	/* Max index into result->spec */

    /* Variables */
    double step	  = 1 / ppsec;
    int	   fftsize;			/* == speclen * 2 */
    spectrum *spec;
    double  t;				/* Time from start of piece */

    if (to == 0.0) to = length;

    fftsize = speclen * 2;	/* Not sure that an odd fftsize would work */

    spec = create_spectrum(speclen, calc->window);
    if (spec == NULL) {
	fprintf(stderr, "Can't create spectrum.\n");
	return;
    }

    for (t = from; t <= to + DELTA; t += step) {
        result_t *result;	/* The result structure */

	result = (result_t *) malloc(sizeof(result_t));
	if (!result) {
	    fprintf(stderr, "Out of memory in calc()\n");
	    return;
	}

	result->t = t;
	result->speclen = speclen;

	/* Fetch the appropriate audio for our FFT source */
	/* The data is centred on the requested time. */
	read_mono_audio_double(calc->audio_file, spec->time_domain,
			       lrint(t * sr) - fftsize/2, fftsize);

	calc_magnitude_spectrum(spec);

	/* We need to pass back a buffer obtained from malloc() that will
	 * subsequently be freed or kept. Rather than memcpy() it, we hijack
	 * the already-allocated buffer and malloc a new one for next time.
	 */
	result->spec = spec->mag_spec;
	spec->mag_spec = calloc(speclen + 1, sizeof (*spec->mag_spec));
	if (spec->mag_spec == NULL) {
	    fprintf(stderr, "Out of memory in calc()\n");
	    return;
	}

	/* Mark the converted data as not having been calculated yet */
	result->mag = NULL;

	/* Send result to main loop */
	(*result_cb)(result);
    }
    destroy_spectrum(spec);
}
