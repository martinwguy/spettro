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
#include "calc.h"
#include "spectrum.h"

static void read_audio(AFfilehandle af, double *data, int start, int datalen);

/*
 * The compute-FFTs function
 *
 * Results are returned in a result_t (struct result) which is obtained from
 + malloc, as is the "spec" field of it.
 */
void
calc_heavy(void *data, Ecore_Thread *thread)
{
    calc_t *calc  = (calc_t *)data;

    /* The real function parameters */
    AFfilehandle af = calc->af;		/* the audiofile */
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

    /* We want the data as mono floats. I don't know if libaudiofile averages
     * stereo to make mono or drops all but the first channel, so we may have
     * to read all the channels and average them.
     */
    if (afSetVirtualSampleFormat(af, AF_DEFAULT_TRACK, AF_SAMPFMT_DOUBLE,
						       sizeof(double)) ||
        afSetVirtualChannels(af, AF_DEFAULT_TRACK, 1)) {
	    fprintf(stderr, "Can't set virtual sample format.\n");
	    return;
    }

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
	read_audio(af, spec->time_domain, lrint(t * sr) - fftsize/2, fftsize);

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

	/* Send result to main loop */
	ecore_thread_feedback(thread, result);
    }
    destroy_spectrum(spec);
}

/*
 * Helper function from sndfile-tools/src/spectrogram.c reads mono audio data
 * from the sound file.
 *
 * "start" is the index of the first sample frame to read and may be negative.
 * "datalen" is the number of sample frames to read.
 * "data" is were to put them.
 */
static void
read_audio(AFfilehandle af, double *data, int start, int datalen)
{
    int frames;	/* Number of frames returned by afReadFrames() */

    memset(data, 0, datalen * sizeof (data [0]));

    if (start >= 0) {
	afSeekFrame(af, AF_DEFAULT_TRACK, start);
    } else {
	start = -start;
	afSeekFrame(af, AF_DEFAULT_TRACK, 0);
	data += start;
	datalen -= start;
    }
    do {
	frames = afReadFrames(af, AF_DEFAULT_TRACK, (void *)data, datalen);
	if (frames > 0) {
	    data += frames;
	    datalen -= frames;
	} else {
	    /* We ask it to read past EOF so failure is normal */
	}
    } while (datalen > 0 && frames > 0);
}
