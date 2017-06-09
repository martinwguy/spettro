/*
 * interpolate.c - map from the linear FFT magnitudes to the magnitudes
 * required for display.  Log frequency axis distortion is also done here.
 */

#include <math.h>

#include "spettro.h"

/* Helper function:
 * Map the index for an output pixel in a column to an index into the
 * FFT result representing the same frequency.
 * magindex is from 0 to maglen-1, representing min_freq to max_freq Hz.
 * Return values from are from 0 to speclen representing frequencies from
 * 0 to the Nyquist frequency.
 * The result is a floating point number as it may fall between elements,
 * allowing the caller to interpolate onto the input array.
 */
static double
magindex_to_specindex(int speclen, int maglen, int magindex,
		      double min_freq, double max_freq,
		      int sample_rate, bool log_freq)
{
	double freq; /* The frequency that this output value represents */

	if (!log_freq)
	    freq = min_freq + (max_freq - min_freq) * magindex / (maglen - 1);
	else
	    freq = min_freq * pow(max_freq / min_freq, (double) magindex / (maglen - 1));

	return (freq * speclen / (sample_rate / 2));
}

/*
 * interpolate()
 *
 * Map values from the spectrogram onto an array of magnitudes for display.
 * Reads spec[0..speclen], representing linearly 0Hz tp sample_rate/2
 * Writes mag[0..maglen-1], representing min_freq to max_freq.
 * If log_freq, te output frequency axis if logarithmic instead of linear.
 *
 * Returns the maximum value in the result.
 */
float
interpolate(float* mag, int maglen, const float *spec, const int speclen,
	    const double min_freq, const double max_freq,
	    const double sample_rate, const bool log_freq)
{
    float max = 0.0;
    int k;

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

    for (k = 0; k < maglen; k++) {
	/* Average the pixels in the range it comes from */
	double this = magindex_to_specindex(speclen, maglen, k,
			min_freq, max_freq, sample_rate, log_freq);
	double next = magindex_to_specindex(speclen, maglen, k+1,
			min_freq, max_freq, sample_rate, log_freq);

	/* Range check: can happen if max_freq > sample_rate / 2 */
	if (this > speclen) {
	    mag [k] = 0.0;
	    return max;
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

	    mag [k] = sum / count;
	} else {
	    /* The output indices are more densely packed than the
	     * input indices so interpolate between input values
	     * to generate more output values.
	     */
	    /* Take a weighted average of the nearest values */
	    mag[k] = spec[(int) this] * (1.0 - (this - floor (this)))
		   + spec[(int) this + 1] * (this - floor (this));
	}
	if (mag[k] > max) max = mag[k];
    }
    return(max);
}
