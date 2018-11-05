/*
 * calc.c - Do all the heavy calculation of spectra.
 *
 * This file's only entry point is calc(), which is handed a calc_t
 * describing the transform to perform, does the FFT, creates a result_t
 * and hands it to calc_result(), which sends them via a GUI event
 * to the main loop, which passes them to its calc_notify().
 * The logarithmic frequency axis is applied and coloring done there, not here,
 * so as not to have to recalculate the FFT for zooms, pans and recoloring.
 *
 * When we're finished with the calc_t, it's up to us to free it.
 */

#include "spettro.h"

#include <unistd.h>	/* for usleep() */
#include <math.h>

#if ECORE_MAIN
#include <Ecore.h>
#elif SDL_MAIN
#include <SDL.h>
#include <SDL_events.h>
#endif

#include "audio_file.h"
#include "audio_cache.h"
#include "cache.h"
#include "calc.h"
#include "gui.h"	/* For RESULT_EVENT */
#include "lock.h"
#include "speclen.h"
#include "spectrum.h"
#include "main.h"	/* for window_function */

/*
 * The compute-FFTs function
 *
 */

/* Helper functions */
static void calc_result(result_t *result);
static result_t *get_result(calc_t *calc, spectrum *spec);

void
calc(calc_t *calc)
{
    spectrum *spec;

    /* If parameters have changed since the work was queued, use the new ones */
    if (calc->speclen != speclen || calc->window != window_function) {
	free(calc);
	return;
    }

    spec = create_spectrum(calc->speclen, calc->window);
    if (spec == NULL) {
	fprintf(stderr, "Can't create spectrum.\n");
	return;
    }

    calc_result(get_result(calc, spec));

    destroy_spectrum(spec);
}

/* The function called by calculation threads to report a result */
static void
calc_result(result_t *result)
{
    /* Send result back to main loop */
#if ECORE_MAIN
    /* Don't return a result if our thread has a cancel request */
    if (ecore_thread_check(result->thread) == FALSE)
	ecore_thread_feedback(result->thread, result);
#elif SDL_MAIN
    SDL_Event event;
    event.type = SDL_USEREVENT;
    event.user.code = RESULT_EVENT;
    event.user.data1 = result;
    while (SDL_PushEvent(&event) != SDL_PUSHEVENT_SUCCESS) {
	/* The SDL1.2 queue length is 127, and all 127 events are
	 * for our user event number 24 */
	usleep(10000); /* Sleep for a 1/100th of a second and retry */
    }
#endif
}

/*
 * Calculate the magnitude spectrum for a column
 */
static result_t *
get_result(calc_t *calc, spectrum *spec)
{
        result_t *result;	/* The result structure */
	int fftsize = calc->speclen * 2;

	result = (result_t *) Malloc(sizeof(result_t));

	result->t = calc->t;
	result->speclen = calc->speclen;
	result->window = calc->window;
#if ECORE_MAIN
	result->thread = calc->thread;
#endif

	/* Fetch the appropriate audio for our FFT source */
	/* The data is centred on the requested time. */
	if (!lock_audio_file()) {
	    fprintf(stderr, "Cannot lock audio file\n");
	    exit(1);
	}
	read_cached_audio(calc->audio_file, (char *) spec->time_domain,
			  af_double, 1,
			  lrint(calc->t * calc->sr) - fftsize/2, fftsize);
	if (!unlock_audio_file()) {
	    fprintf(stderr, "Cannot unlock audio file\n");
	    exit(1);
	}

	calc_magnitude_spectrum(spec);

	/* We need to pass back a buffer obtained from malloc() that will
	 * subsequently be freed or kept. Rather than memcpy() it, we hijack
	 * the already-allocated buffer and malloc a new one for next time.
	 */
	result->spec = spec->mag_spec;
	spec->mag_spec = Malloc((calc->speclen+1) * sizeof(*(spec->mag_spec)));

	/* Mark the converted data as not having been allocated yet */
	result->logmag = NULL;

	return(result);
}
