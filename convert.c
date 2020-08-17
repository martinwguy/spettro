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

/* convert.c: Functions to convert one kind of value to another and
 * compute useful values from the global data. */

#include "spettro.h"
#include "convert.h"

#include "audio_file.h"		/* for audio_file->sample_rate */
#include "ui.h"

/*
 * Vertical position (frequency domain) conversion functions
 *
 * The bottom pixel row (min_y) should be centered on the minimum frequency,
 * min_freq, and the top pixel row (max_y) on the maximum frequency, max_freq.
 */

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
double
magindex_to_frequency(int k)
{
    return min_freq * pow(max_freq / min_freq,
    			  (double)k / (maglen - 1));
}

double
frequency_to_specindex(double freq, double sample_rate, int speclen)
{
    return freq * speclen / (sample_rate / 2);
}

/* Convert an audio frequency to its index in the magnitude spectrum.
 * To get the screen pixel row it falls in, add min_y.
 */
int
freq_to_magindex(double freq)
{
    return lrint((log(freq) - log(min_freq)) /
		 (log(max_freq) - log(min_freq)) *
		 (max_y - min_y));
}

/* Take "A0" or whatever and return the frequency it represents.
 * The standard form "C5#" is also recognized and synonym "C5+".
 * Returns NAN if the note name is not recognized.
 */
double
note_name_to_freq(const char *note)
{
    static int semitones[7] = { 0, 2, 3, 5, 7, 8, 10 }; /* A-G */
    bool sharp = note[0] && note[1] && (note[2] == '#' || note[2] == '+');

    if (toupper(note[0]) >= 'A' && toupper(note[0]) <= 'G' &&
	(note[1] >= '0' && note[1] <= '9') &&
	(note[2] == '\0' || (sharp && note[3] == '\0'))) {
	return (A4_FREQUENCY / 16.0) /* A0 */
		* pow(2.0, note[1] - '0') 
		* pow(2.0, (1/12.0) *
		           (semitones[toupper(note[0]) - 'A'] + sharp));
    } else {
        return NAN;
    }
}

/* Convert a note number of the piano keyboard to the frequency it represents.
 * It's the note of an 88-note piano: 0 = Bottom A, 87 = top C
 */
double
note_number_to_freq(const int n)
{
    static double cache[88];	/* Init to 0.0 */
    if (cache[n] == 0.0)
	cache[n] = (A4_FREQUENCY / 16.0) /* A0 */
		   * pow(2.0, (1/12.0) * n);
    return cache[n];
}

/*
 * Horizontal position (time domain) conversion functions
 *
 * We divide time into steps, one for each pixel column, starting from the
 * start of the piece of audio, with column 0 of the piece representing
 * what happens from 0.0 to 1/ppsec seconds.
 * The audio for the FFT for a given column should therefore be centered
 * on 
 *
 * disp_time, instead, is the exact time that we should be displaying at the
 * green line, in such a way that that exact time falls within the time
 * covered by that pixel column.
 */

/* Convert a time in seconds to the screen column in the whole piece that
 * contains this moment. */
int
time_to_piece_column(double t)
{
    return (int) floor(t / secpp + DELTA);
}

int
time_to_screen_column(double t)
{
    return time_to_piece_column(t - disp_time) + disp_offset;
}

/* What time does the left edge of this screen column represent? */
double
screen_column_to_start_time(int col)
{
    return disp_time + (col - disp_offset) * secpp;
}

/*
 *	Choose a good FFT size for the given FFT frequency
 */

/* Helper functions */
static bool is_good_speclen(int n);
static bool is_2357(int n);

int
fft_freq_to_speclen(double fft_freq, double sample_rate)
{
    int speclen = (sample_rate / fft_freq + 1) / 2;
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

/* Convert time in seconds to a string like 1:30.45 */
char *
seconds_to_string(double secs)
{
    unsigned h,m,s,f;	/* Hours, minutes, seconds and hundredths of a sec */
    unsigned isecs;	/* Number of whole seconds */
    static char string[sizeof("-HH:MM:SS.ff")]; /* Up to 99 hours */
    char *str = string;

    if (secs < 0.0) {
	/* Cannot happen (unless they set a bar line before time=0?)
	 * Do the right thing anyway. */
	string[0] = '-';
	str++;
	secs = -secs;
    }

    /* round to the nearest hundredth of a second */
    secs = round(secs * 100) / 100;
    isecs = (unsigned) trunc(secs);
    f = lrint((secs - (double)isecs) * 100);
    s = isecs % 60;
    m = (isecs/60) % 60;
    h = isecs/60/60;

    {
	/* Avoid gcc compiler warning about possible sprintf buffer overflow.
	 * Sorry if your audio file is more than 255 hours long! */
	unsigned char ch = h;

	if (h > 0) sprintf(str, "%d:%02d:%02d.%02d", ch, m, s, f);
	else if (m > 0) sprintf(str, "%d:%02d.%02d", m, s, f);
	else sprintf(str, "%d.%02d", s, f);
    }

    return string;
}

/* Convert a time string to a double.
 * The time may be any number of seconds (and maybe a dot and decimal places)
 * or minutes:SS[.dp] or hours:MM:SS[.dp]
 *
 * If the string argument is not parsable, we return NAN.
 *
 * Other oddities forced by the use of sscanf():
 * Minutes with hours, and seconds with minutes, can also be a single digit.
 * Because %u and %f always discard initial white space, they can put spaces
 * after a colon or before the decimal point.
 * Seconds before a decimal point are %u instead of %2u because otherwise
 * "1:400.5" would be accepted as 1:40.5
 * "1:00:" is accepted as "1:00".
 */
double
string_to_seconds(char *string)
{
    unsigned h, m, s;	/* Hours, minutes and seconds */
    double frac = 0.0;	/* decimal places, 0 <= frac < 1.0 */
    double secs;	/* Result for just the seconds */
    int n;		/* Number of chars were consumed by the match,
    			 * used to detect trailing garbage */

    if (sscanf(string, "%2u:%2u:%u%lf%n", &h, &m, &s, &frac, &n) == 4) { }
    else
    if (sscanf(string, "%2u:%2u:%u%n", &h, &m, &s, &n) == 3) { }
    else
    if (sscanf(string, "%2u:%u%lf%n", &m, &s, &frac, &n) == 3)
	h = 0;	/* May have been set by a previous partial match */
    else
    if (sscanf(string, "%2u:%2u%n", &m, &s, &n) == 2)
	h = 0;
    else
    if (sscanf(string, "%lf%n", &secs, &n) == 1 &&
	    secs >= 0.0 && DELTA_LT(secs, (double)(99*60*60 + 59*60 + 60)) &&
	    string[n] == '\0')
	return secs;
    else
	return NAN;

    /* Handle all formats except a bare number of seconds */

    /* Range checks. Hours are limited to 2 digits when printed. */
    if (s > 59 || m > 59 || h > 99 || frac < 0.0 || frac >= 1.0 ||
	    string[n] != '\0')
	return NAN;

    return h*60*60 + m*60 + s + frac;
}
