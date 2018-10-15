/*
 * spectrum.c: Calculate FFT spectra of audio data.
 *
 * Adapted from sndfile-tools/src/spectrum.c
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <fftw3.h>
#include <sndfile.h>

#include "spettro.h"
#include "window.h"
#include "spectrum.h"
#include "lock.h"

spectrum *
create_spectrum (int speclen, enum WINDOW_FUNCTION window_function)
{
    spectrum *spec;

    spec = calloc(1, sizeof(spectrum));
    if (spec == NULL) return(NULL);

    spec->wfunc = window_function;
    spec->speclen = speclen;

    /*
     * mag_spec has values from [0..speclen] inclusive for 0Hz to Nyquist.
     * time_domain has an extra element to be able to interpolate between
     * samples for better time precision, hoping to eliminate artifacts.
     */
    lock_fftw3();
    spec->time_domain	= fftw_alloc_real(2 * speclen + 1);
    spec->freq_domain	= fftw_alloc_real(2 * speclen);
    unlock_fftw3();
    spec->mag_spec	= calloc(speclen + 1,	  sizeof(*spec->mag_spec));
    spec->plan = NULL;
    if (spec->time_domain == NULL ||
	spec->freq_domain == NULL ||
	spec->mag_spec == NULL) {
	    destroy_spectrum(spec);
	    return(NULL);
    }

    lock_fftw3();
    spec->plan = fftw_plan_r2r_1d(2 * speclen,
			    spec->time_domain, spec->freq_domain,
			    FFTW_R2HC, FFTW_ESTIMATE /*| FFTW_PRESERVE_INPUT*/);
    unlock_fftw3();

    if (spec->plan == NULL) {
	fprintf(stderr, "create_spectrum(): failed to create plan\n");
	destroy_spectrum(spec);
	return(NULL);
    }

    spec->window = get_window(spec->wfunc, 2 * speclen);

    return spec;
}

void
destroy_spectrum(spectrum *spec)
{
    lock_fftw3();
    if (spec->plan) {
	fftw_destroy_plan(spec->plan);
    }
    fftw_free(spec->time_domain);
    fftw_free(spec->freq_domain);
    unlock_fftw3();
    /* free(spec->window);	window may be in use by another calc thread */
    free(spec->mag_spec);
    free(spec);
}

void
calc_magnitude_spectrum(spectrum *spec)
{
    int k, freqlen;
    int speclen = spec->speclen;

    freqlen = 2 * speclen;

    if (spec->wfunc != RECTANGULAR)
	for (k = 0; k < 2 * speclen; k++)
	    spec->time_domain[k] *= spec->window[k];

    fftw_execute(spec->plan);

    /*
     * Convert from FFTW's "half complex" format to an array of magnitudes.
     * In HC format, the values are stored:
     * r0, r1, r2 ... r(n/2), i(n+1)/2-1 .. i2, i1
     */

    /* Add the DC offset at 0Hz */
    spec->mag_spec[0] = fabs(spec->freq_domain[0]);

    for (k = 1; k < speclen; k++) {
	double re = spec->freq_domain[k];
	double im = spec->freq_domain[freqlen - k];
	double mag = sqrt(re * re + im * im);
	spec->mag_spec[k] = mag;
    }

    /* Lastly add the point for the Nyquist frequency */
    {
	double mag = fabs(spec->freq_domain[speclen]);
	spec->mag_spec[speclen] = mag;
    }
}
