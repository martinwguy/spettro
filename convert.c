/* convert.c: Functions to convert one kind of value to another and
 * compute useful values from the global data. */

#include "spettro.h"
#include "convert.h"

#include "main.h"

#include <math.h>

/* Return the frequency ratio between one pixel row and the one above,
 * used to scroll/zoom by one pixel.
 * e.g. If the graph is 480 pixels high, it's the 479th root of the ratio
 * between the top and bottom frequencies.
 */
double
v_pixel_freq_ratio()
{
    return pow(max_freq / min_freq, 1.0 / (max_y - min_y));
}

/* What frequency does the centre of this magnitude index represent? */
double magindex_to_frequency(int k)
{
    return min_freq * pow(max_freq / min_freq,
    			  (double)k / (maglen - 1));
}

double frequency_to_specindex(double freq)
{
    return freq * speclen / (sample_rate/2);
}

/*
 *	Choose a good FFT size for the given fft frequency
 */

/* Helper functions */
static bool is_good_speclen(int n);
static bool is_2357(int n);

int
fft_freq_to_speclen(double fft_freq)
{
    int speclen = (sample_rate/fft_freq + 1) / 2;
    int d; /* difference between ideal speclen and preferred speclen */

    /* Find the nearest fast value for the FFT size. */

    for (d = 0 ; /* Will terminate */ ; d++) {
	/* Logarithmically, the integer above is closer than
	 * the integer below, so prefer it to the one below.
	 */
	if (is_good_speclen(speclen + d)) {
	    speclen += d;
	    break;
	}
	if (is_good_speclen(speclen - d)) {
	    speclen -= d;
	    break;
	}
    }

    return speclen;
}

/*
 * Helper function: is N a "fast" value for the FFT size?
 *
 * We use fftw_plan_r2r_1d() for which the documentation
 * http://fftw.org/fftw3_doc/Real_002dto_002dReal-Transforms.html says:
 *
 * "FFTW is generally best at handling sizes of the form
 *      2^a 3^b 5^c 7^d 11^e 13^f
 * where e+f is either 0 or 1, and the other exponents are arbitrary."
 *
 * Our FFT size is 2*speclen, but that doesn't affect these calculations
 * as 2 is an allowed factor and an odd fftsize may or may not work with
 * the "half complex" format conversion in calc_magnitudes().
 */

static bool
is_good_speclen (int n)
{
    /* It wants n, 11*n, 13*n but not (11*13*n)
    ** where n only has as factors 2, 3, 5 and 7
     */
    if (n % (11 * 13) == 0) return 0; /* No good */

    return is_2357(n) || ((n % 11 == 0) && is_2357(n / 11))
		      || ((n % 13 == 0) && is_2357(n / 13));
}

/* Helper function: does N have only 2, 3, 5 and 7 as its factors? */
static bool
is_2357(int n)
{
    /* Eliminate all factors of 2, 3, 5 and 7 and see if 1 remains */
    while (n % 2 == 0) n /= 2;
    while (n % 3 == 0) n /= 3;
    while (n % 5 == 0) n /= 5;
    while (n % 7 == 0) n /= 7;
    return (n == 1);
}

/* Take "A0" or whatever and return the frequency it represents */
double
note_name_to_freq(const char *note)
{
    static int semitones[7] = { 0, 2, 3, 5, 7, 8, 10 }; /* A-G */

    if (note[0] < 'A' || note[0] > 'G' || note[1] < '0' || note[1] > '9')
        return NAN;

    return A0_FREQUENCY *
    	   pow(2.0, note[1] - '0') * 
	   pow(2.0, (1/12.0) * semitones[note[0] - 'A']);
}

/* Convert a note number of the piano keyboard to the frequency it represents.
 * It's the note of an 88-note piano: 0 = Bottom A, 87 = top C
 */
double
note_number_to_freq(const int n)
{
    static double cache[88];	/* Init to 0.0 */
    if (cache[n] == 0.0)
	cache[n] = A0_FREQUENCY * pow(2.0, (1/12.0) * n);
    return cache[n];
}

/* Convert an audio frequency to the pixel row it is represented by */
int
freq_to_magindex(double freq)
{
    return lrint((log(freq) - log(min_freq)) /
		 (log(max_freq) - log(min_freq)) *
		 (disp_height - 1));
}
