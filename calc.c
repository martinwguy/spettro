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

#include "convert.h"

#include <unistd.h>	/* for usleep() */

#if ECORE_MAIN
#include <Ecore.h>
#elif SDL_MAIN
#include <SDL.h>
#include <SDL_events.h>
#endif

#include "audio_cache.h"
#include "cache.h"
#include "calc.h"
#include "gui.h"	/* For RESULT_EVENT */
#include "lock.h"
#include "spectrum.h"
#include "ui.h"

/*
 * The compute-FFTs function
 */

/* Helper functions */
static void calc_result(result_t *result);
static result_t *get_result(calc_t *calc, spectrum *spec, int speclen);

void
calc(calc_t *calc)
{
    spectrum *spec;
    int speclen	= fft_freq_to_speclen(calc->fft_freq,
    				      current_sample_rate());
    result_t *result;

    /* If parameters have changed since the work was queued, don't bother.
     * This should never happen because we clear the work queue when we
     * change these parameters */
    if (calc->window != window_function || calc->fft_freq != fft_freq) {
	free(calc);
	return;
    }

    spec = create_spectrum(speclen, calc->window);
    if (spec == NULL) {
	fprintf(stderr, "Can't create spectrum.\n");
	return;
    }

    result = get_result(calc, spec, speclen);
    if (result != NULL) calc_result(result);

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
 * Calculate the magnitude spectrum for a column.
 *
 * speclen is precalculated by the caller from calc->fft_freq
 */
static result_t *
get_result(calc_t *calc, spectrum *spec, int speclen)
{
        result_t *result;	/* The result structure */
	int fftsize = speclen * 2;

	result = (result_t *) Malloc(sizeof(result_t));

	result->t = calc->t;
	result->fft_freq = calc->fft_freq;
	result->speclen = speclen;
	result->window = calc->window;
#if ECORE_MAIN
	result->thread = calc->thread;
#endif

	/* Fetch the appropriate audio for our FFT source */
	/* The data is centred on the requested time. */
	if (read_cached_audio((char *) spec->time_domain, af_float, 1,
			      lrint(calc->t * current_sample_rate()) - fftsize/2,
			      fftsize) != fftsize) {
	    fprintf(stderr, "calc thread can't read %d samples at %ld\n",
	    	    fftsize,
		    lrint(calc->t * current_sample_rate()) - fftsize/2);
	    free(result);
	    return NULL;
	}

	calc_magnitude_spectrum(spec);

	/* We need to pass back a buffer obtained from malloc() that will
	 * subsequently be freed or kept. Rather than memcpy() it, we hijack
	 * the already-allocated buffer and malloc a new one for next time.
	 */
	result->spec = spec->mag_spec;
	spec->mag_spec = Malloc((speclen + 1) * sizeof(*(spec->mag_spec)));

	return(result);
}
