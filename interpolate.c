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
 * interpolate.c - map from the linear FFT magnitudes to the magnitudes
 * required for display.  Log frequency axis distortion is also done here.
 */


#include "spettro.h"

// #include "audio_file.h"		/* for audio_file->sample_rate */
#include "convert.h"
#include "ui.h"

/* Helper function:
 * Map the index for an output pixel in a column to an index into the
 * FFT result representing the same frequency.
 * magindex is from 0 to maglen-1, representing min_freq to max_freq Hz
 * displayed at pixel rows min_y to max_y
 * Return values from are from 0 to speclen representing frequencies from
 * 0 to the Nyquist frequency.
 * The result is a floating point number as it may fall between elements,
 * allowing the caller to interpolate between adjacent elements
 * of the input array.
 *
 * This is called so often with the same values, and pow() is so expensive,
 * that it's better to precalculate the mappings and return them from an array.
 */
static double *mtoscache = NULL;
static int mtoscache_size = 0;
static int mtoscache_speclen = 0;
static int mtoscache_maglen = 0;
static double mtoscache_min_freq = 0.0;
static double mtoscache_max_freq = 0.0;
static double mtoscache_sample_rate = 0.0;

/* What index in the linear spectrum does pixel row "magindex" correspond to?
 * "magindex" can be from 0..maglen, not 0..maglen-1, because the
 * interpolator may use the row above the top one (is this right?).
 */
static double
magindex_to_specindex(int magindex, double sample_rate, int speclen)
{
    /* Recalculate the array of values if any of the parameters changed */
    if (speclen != mtoscache_speclen
     || maglen != mtoscache_maglen
     || min_freq != mtoscache_min_freq
     || max_freq != mtoscache_max_freq
     || sample_rate != mtoscache_sample_rate) {
	int k;

	if (maglen != mtoscache_maglen)
	    mtoscache = Realloc(mtoscache, (maglen+1) * sizeof(double));

	for (k=0; k <= maglen; k++) {
	    /* The actual conversion function */
	    double freq = magindex_to_frequency(k);
	    mtoscache[k] = frequency_to_specindex(freq, sample_rate, speclen);
	}

	mtoscache_speclen = speclen;
	mtoscache_maglen = maglen;
	mtoscache_min_freq = min_freq;
	mtoscache_max_freq = max_freq;
	mtoscache_sample_rate = sample_rate;
    }

    if (magindex < 0 || magindex > maglen) {
	    fprintf(stderr, "Invalid magindex of %d\n", magindex);
	    abort();
    }

    return mtoscache[magindex];
}

void
free_interpolate_cache()
{
    free(mtoscache);
    mtoscache = NULL;
    mtoscache_size = 0;
}

/*
 * interpolate()
 *
 * Map values from the spectrogram onto an array of magnitudes for display.
 * Reads spec[0..speclen], representing linearly 0Hz tp sample_rate/2
 * Writes logmag[0..maglen-1], representing min_freq to max_freq.
 * from_y and to_y limit the range of diplay rows to fill
 * (== min_y and max_y-1 to paint the whole column).
 *
 * Returns the maximum value seen so far.
 */

double
interpolate(float* logmag, const float *spec, const int from_y, const int to_y, double sample_rate, int speclen)
{
    int y;

    /* Map each output coordinate to where it depends on in the input array.
     * If there are more input values than output values, we need to average
     * a range of inputs.
     * If there are more output values than input values we do linear
     * interpolation between the two inputs values that a reverse-mapped
     * output value's coordinate falls between.
     *
     * spec points to an array with elements [0..speclen] inclusive
     * representing frequencies from 0 to sample_rate/2 Hz. Map these to the
     * scale values min_freq to max_freq so that the bottom and top pixels
     * in the output represent the energy in the sound at min_ and max_freq Hz.
     */

    for (y = from_y; y <= to_y; y++) {
    	int k = y - min_y;	/* Index into magnitude array */
	double this = magindex_to_specindex(k, sample_rate, speclen);
	double next = magindex_to_specindex(k + 1, sample_rate, speclen);

	/* Range check: can happen if max_freq > sample_rate / 2 */
	if (this > speclen) {
	    logmag[k] = -INFINITY;
	    continue;
	}

	if (next > this + 1) {
	    /* The output indices are more sparse than the input indices
	     * so average the range of input indices that map to this output,
	     * making sure not to exceed the input array (0..speclen inclusive)
	     */
	    /* Take a proportional part of the first sample */
	    double count = 1.0 - (this - floor (this));
	    double sum = spec [(int) this] * count;

	    while ((this += 1.0) < next && (int) this <= speclen) {
	    	sum += spec [(int) this];
		count += 1.0;
	    }
	    /* and part of the last one */
	    if ((int) next <= speclen) {
	        sum += spec [(int) next] * (next - floor (next));
		count += next - floor (next);
	    }

	    logmag[k] = log10(sum / count);
	} else {
	    /* The output indices are more densely packed than the
	     * input indices so interpolate between input values
	     * to generate more output values.
	     */
	    /* Take a weighted average of the nearest values */
	    logmag[k] = log10(spec[(int) this] * (1.0 - (this - floor (this)))
		              + spec[(int) this + 1] * (this - floor (this)));
	}
	if (logmag[k] > logmax) {
	    logmax = logmag[k];
	}
    }
    return(logmax);
}
