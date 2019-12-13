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

/* axes.c: Stuff to draw axes at the edges of the graphic.
 *
 * There is a numeric Hertz frequency axis on the left, a note name
 * frequency axis on the right (A0, B0, C1, D1 etc)
 * along the bottom the time axis in seconds (actually just the times at the
 * current playing position, the left/rightmost edges  and maybe bar lines)
 * and along the top various status information (FFT freq, window etc).
 *
 * The corners belong to the frequency axes: if the topmost pixel row has a
 * numbered tick, the text will overflow into the gap, while the leftmost and
 * rightmost times are left and right aligned so do not overflow the corners.
 */

#include "spettro.h"
#include "axes.h"

#include "audio.h"
#include "barlines.h"
#include "convert.h"
#include "gui.h"
#include "text.h"
#include "ui.h"
#include "window.h"

#include <string.h>

/* Array of numbers to print, or NO_NUMBER */
/* Where to put each tick/label on the y-axis */
typedef struct {
    double value;	/* No to show, NO_NUMBER if just a tick and no number */
    double distance;	/* Distance of tick from min_y */
} tick_t;
static tick_t *ticks = NULL;
static int number_of_ticks = 0;	/* Number of labels allocated in this array */

/* The digit that changes from label to label.
 * This ensures that a range from 999 to 1001 prints 999.5 and 1000.5
 * instead of 999 1000 1000 1000 1001.
 */
static int decimal_places_to_print;

/* Value to store in the ticks[k].value field to mean
 * "Put a tick here, but don't print a number."
 * NaN (0.0/0.0) is untestable without isnan() so use a random value.
 */
#define NO_NUMBER (M_PI)		/* They're unlikely to hit that! */

/* Is this entry in "ticks" one of the numberless ticks? */
#define JUST_A_TICK(ticks, k)	(ticks[k].value == NO_NUMBER)

/* Forward declarations */

static int calculate_ticks(double min, double max, double distance, int log_scale);
static int calculate_log_ticks(double min, double max, double distance);
static void draw_freq_axis(void);
static void draw_note_names(void);

void
draw_axes(void)
{
    if (show_freq_axes)
	draw_freq_axes();

    if (show_time_axes) {
	draw_status_line();
	draw_time_axis();
    }
}

void
draw_freq_axes(void)
{
    draw_freq_axis();
    draw_note_names();
}

void
draw_time_axes(void)
{
    draw_status_line();
    draw_time_axis();
}

/* Put ticks on Frequency axis. Only called when show_axes is TRUE. */
static void
draw_freq_axis()
{
    int tick_count = calculate_ticks(min_freq, max_freq, max_y - min_y, 1);
    int i;

    gui_paint_rect(0, 0, min_x - 1, disp_height - 1, black);

    gui_lock();
    for (i=0; i < tick_count; i++) {
	char s[16];	/* [6] is probably enough */
	gui_putpixel(min_x - 1, min_y + lrint(ticks[i].distance), green);
	gui_putpixel(min_x - 2, min_y + lrint(ticks[i].distance), green);
	if (ticks[i].value != NO_NUMBER) {
	    char *spacep;
	    int width;
	    /* Left-align the number in the string, remove trailing spaces */
	    sprintf(s, "%-5g", ticks[i].value);
	    if ((spacep = strchr(s, ' ')) != NULL) *spacep = '\0';

	    /* If the text is wider than the axis, grow the axis */
	    if ((width = 1 + text_width(s) + 1 + 2) > frequency_axis_width) {
	    	min_x = frequency_axis_width = width;
    		gui_unlock();
		draw_freq_axis();
		return;
	    }

	    draw_text(s, min_x - 4, min_y + lrint(ticks[i].distance),
		      RIGHT, CENTER);
	}
    }
    gui_unlock();
    gui_update_rect(0, 0, frequency_axis_width - 1, disp_height - 1);
}

static void
draw_note_names()
{
    char note_name[3]; /* "A0" etc */

    gui_paint_rect(max_x + 1, 0, disp_width - 1, disp_height - 1, black);

    note_name[2] = '\0';
    gui_lock();
    for (note_name[0] = 'A'; note_name[0] <= 'G'; note_name[0]++) {
	for (note_name[1] = '0'; note_name[1] <= '9'; note_name[1]++) {
	    double freq = note_name_to_freq(note_name);
	    double half_a_pixel = sqrt(v_pixel_freq_ratio());
	    /* If the tick is within the axis range, draw it.
	     * The half pixel slop ensures that the top and bottom pixel rows
	     * receive a tick if the vertical pixel position of that label
	     * would round to the pixel row in question. Otherwise half the
	     * time at random the top or bottom pixel rows are not labelled.
	     */
	    if (DELTA_GE(freq, min_freq / half_a_pixel) &&
	        DELTA_LE(freq, max_freq * half_a_pixel)) {
		int y = min_y + freq_to_magindex(freq);
		gui_putpixel(max_x + 1, y, green);
		gui_putpixel(max_x + 2, y, green);
		draw_text(note_name, max_x + 4, y, LEFT, CENTER);
	    }
	}
    }
    gui_unlock();
    gui_update_rect(max_x + 1, 0, disp_width - 1, disp_height - 1);
}

/* Decide where to put ticks and numbers on an axis.
 *
 * Graph-labelling convention is that the least significant digit that changes
 * from one label to the next should change by 1, 2 or 5, so we step by the
 * largest suitable value of 10^n * {1, 2 or 5} that gives us the required
 * number of divisions / numeric labels.
 */

/* The old code used to make 6 to 14 divisions and number every other tick.
 * What we now mean by "division" is one of the gaps between numbered segments
 * so we ask for a minimum of 3 to give the same effect as the old minimum of
 * 6 half-divisions.
 * This results in the same axis labelling for all maximum values
 * from 0 to 12000 in steps of 1000 and gives sensible results from 13000 on,
 * to a maximum of 7 divisions and 8 labels from 0 to 14000.
 */
#define TARGET_DIVISIONS 3

/* Helper function */
static int
add_tick(int k, double min, double max, double distance, int log_scale, double val, bool just_a_tick)
{
    double range = max - min;

    if (k >= number_of_ticks) {
	ticks = Realloc(ticks, (k+1) * sizeof(*ticks));
	number_of_ticks = k+1;
    }

    if (DELTA_GE(val, min) && DELTA_LE(val, max)) {
	ticks[k].value = just_a_tick ? NO_NUMBER : val;
	ticks[k].distance =
	    distance * (log_scale == 2
		/*log*/	? (log(val) - log(min)) / (log(max) - log(min))
		/*lin*/	: (val - min) / range);
	k++;
    }
    return k;
}

/* log_scale is pseudo-boolean:
 * 0 means use a linear scale,
 * 1 means use a log scale and
 * 2 is an internal value used when calling back from calculate_log_ticks() to
 *   label the range with linear numbering but logarithmic spacing.
 */
static int
calculate_ticks(double min, double max, double distance, int log_scale)
{
    double stride;	/* Put numbered ticks at multiples of this */
    double range = max - min;
    int k;
    double value;	/* Temporary */

    if (log_scale == 1)
	    return calculate_log_ticks(min, max, distance);

    /* Linear version */

    /* Choose a step between successive axis labels so that one digit
     * changes by 1, 2 or 5 amd that gives us at least the number of
     * divisions (and numberic labels) that we would like to have.
     *
     * We do this by starting "stride" at the lowest power of ten <= max,
     * which can give us at most 9 divisions (e.g. from 0 to 9999, step 1000)
     * Then try 5*this, 2*this and 1*this.
     */
    stride = pow(10.0, floor(log10(max)));
    do {
	if (range / (stride * 5) >= TARGET_DIVISIONS) {
	    stride *= 5;
	    break;
	}
	if (range / (stride * 2) >= TARGET_DIVISIONS) {
	    stride *= 2;
	    break;
	}
	if (range / stride >= TARGET_DIVISIONS) break;
	stride /= 10;
    } while (1);	/* This is an odd loop! */

    /* Ensure that the least significant digit that changes gets printed, */
    decimal_places_to_print = lrint(-floor(log10(stride)));
    if (decimal_places_to_print < 0)
	    decimal_places_to_print = 0;

    /* Now go from the first multiple of stride that's >= min to
     * the last one that's <= max. */
    k = 0;
    value = ceil(min / stride) * stride;

    /* Add the half-way tick before the first number if it's in range */
    k = add_tick(k, min, max, distance, log_scale, value - stride / 2, TRUE);

    while (DELTA_LE(value, max)) {
	/* Add a tick next to each printed number */
	k = add_tick(k, min, max, distance, log_scale, value, FALSE);

	/* and at the half-way tick after the number if it's in range */
	k = add_tick(k, min, max, distance, log_scale, value + stride/2, TRUE);

	value += stride;
    }

    return k;
}

/* Number/tick placer for logarithmic scales.
 *
 * Some say we should number 1, 10, 100, 1000, 1000 and place ticks at
 * 2,3,4,5,6,7,8,9, 20,30,40,50,60,70,80,90, 200,300,400,500,600,700,800,900
 * Others suggest numbering 1,2,5, 10,20,50, 100,200,500.
 *
 * Ticking 1-9 is visually distinctive and emphasizes that we are using
 * a log scale, as well as mimicking log graph paper.
 * Numbering the powers of ten and, if that doesn't give enough labels,
 * numbering also the 2 and 5 multiples might work.
 *
 * Apart from our [number] and tick styles:
 * [1] 2 5 [10] 20 50 [100]  and
 * [1] [2] 3 4 [5] 6 7 8 9 [10]
 * the following are also seen in use:
 * [1] [2] 3 4 [5] 6 7 [8] 9 [10]  and
 * [1] [2] [3] [4] [5] [6] 7 [8] 9 [10]
 * in https://www.lhup.edu/~dsimanek/scenario/errorman/graphs2.htm
 *
 * This works fine for wide ranges, not so well for narrow ranges like
 * 5000-6000, so for ranges less than a decade we apply the above
 * linear numbering style 0.2 0.4 0.6 0.8 or whatever, but calulating
 * the positions of the legends logarithmically.
 *
 * Alternatives could be:
 * - by powers or two from some starting frequency defaulting to
 *   the Nyquist frequency (22050, 11025, 5512.5 ...) or from some
 *   musical pitch (220, 440, 880, 1760)
 * - with a musical note scale  C0 ' D0 ' E0 F0 ' G0 ' A0 ' B0 C1
 * - with manuscript staff lines, piano note or guitar string overlay.
 */

/* Helper functions: add ticks and labels at start_value and all powers of ten
 * times it that are in the min-max range.
 * This is used to plonk ticks at 1, 10, 100, 1000 then at 2, 20, 200, 2000
 * then at 5, 50, 500, 5000 and so on.
 */
static int
add_log_ticks(double min, double max, double distance,
	      int k, double start_value, bool include_number)
{
    double value;

    /* Include the top and bottom rows if the tick for a frequency label
     * would round to that pixel row */
    double half_a_pixel = sqrt(v_pixel_freq_ratio());

    for (value = start_value; DELTA_LE(value, max * half_a_pixel);
         value *= 10.0) {
	if (k >= number_of_ticks) {
	    ticks = Realloc(ticks, (k+1) * sizeof(*ticks));
	    number_of_ticks = k+1;
	}
	if (DELTA_GE(value, min / half_a_pixel)) {
	    ticks[k].value = include_number ? value : NO_NUMBER;
	    ticks[k].distance = distance * (log(value) - log(min))
					 / (log(max) - log(min));
	    k++;
	}
    }
    return k;
}

static int
calculate_log_ticks(double min, double max, double distance)
{
    int k = 0;	/* Number of ticks we have placed in "ticks" array */
    double underpinning; 	/* Largest power of ten that is <= min */

    /* If the interval is less than a decade, just apply the same
     * numbering-choosing scheme as used with linear axis, with the
     * ticks positioned logarithmically.
     */
    if (max / min < 10.0)
	    return calculate_ticks(min, max, distance, 2);

    /* First hack: label the powers of ten. */

    /* Find largest power of ten that is <= minimum value */
    underpinning = pow(10.0, floor(log10(min)));

    /* Go powering up by 10 from there, numbering as we go. */
    k = add_log_ticks(min, max, distance, k, underpinning, TRUE);

    /* Do we have enough numbers? If so, add numberless ticks at 2 and 5 */
    /* No of labels is n. of divisions + 1 */
    if (k >= TARGET_DIVISIONS + 1) {
	k = add_log_ticks(min, max, distance, k,
			  underpinning * 2.0, FALSE);
	k = add_log_ticks(min, max, distance, k,
			  underpinning * 5.0, FALSE);
    } else {
	int i;
	/* Not enough numbers: add numbered ticks at 2 and 5 and
	 * unnumbered ticks at all the rest */
	for (i = 2; i <= 9; i++)
	    k = add_log_ticks(min, max, distance, k,
			      underpinning * (1.0 * i),
			      i == 2 || i == 5);
    }

    /* Greatest possible number of ticks calculation:
     * The worst case is when the else clause adds 8 ticks with the maximal
     * number of divisions, which is when k == TARGET_DIVISIONS, 3,
     * for example 100, 1000, 10000.
     * The else clause adds another 8 ticks inside each division as well as
     * up to 8 ticks after the last number (from 20000 to 90000)
     * and 8 before to the first (from 20 to 90 in the example).
     * Maximum possible ticks is 3+8+8+8+8=35
     */

    return k;
}

/* Stuff for axes along the top and bottom edges of the screen */

/* Draw the status line along the top of the graph */
void
draw_status_line(void)
{
    char s[128];

    /* First, blank it */
    gui_paint_rect(min_x, max_y+1, max_x, disp_height-1, black);

    gui_lock();
    sprintf(s, "%g - %g Hz   %g octaves   %g dB",
	    min_freq, max_freq,
    	    log(max_freq / min_freq) / log(2.0),
	    dyn_range);
    draw_text(s, min_x, max_y + 2, LEFT, BOTTOM);

    sprintf(s, "%g pixels per second   %g pixels per octave",
    	    ppsec, rint(log(2.0) / log(v_pixel_freq_ratio())));

    draw_text(s, disp_offset, max_y + 2, CENTER, BOTTOM);

    sprintf(s, "%g dB DYNAMIC RANGE", dyn_range);
    draw_text(s, (max_x + disp_offset / 2), max_y + 2, CENTER, BOTTOM);

    sprintf(s, "%s WINDOW AT %g HZ", window_name(window_function), fft_freq);
    draw_text(s, max_x, max_y + 2, RIGHT, BOTTOM);
    gui_unlock();

    gui_update_rect(min_x, max_y + 1, max_x, disp_height - 1);
}

/* Draw the times along the bottom of the graph */
void
draw_time_axis(void)
{
    /* Time of left- and rightmost pixel columns */
    double min_time = screen_column_to_start_time(min_x);
    double max_time = screen_column_to_start_time(max_x);

    /* First, blank it */
    gui_paint_rect(min_x, 0, max_x, min_y-1, black);

    gui_lock();

    /* Current playing time */
    draw_text(seconds_to_string(disp_time), disp_offset, 1, CENTER, BOTTOM);

    /* From */
    if (DELTA_GE(min_time, 0.0)) {
	draw_text(seconds_to_string(min_time), min_x, 1, LEFT, BOTTOM);
    } else {
    	/* Draw 0.0 at wherever it is on-screen */
	int x = time_to_screen_column(0.0);
	char *s = "0.00";

	/* We center it on the left edge but stop it overflowing into
	 * the frequency axis.
	 */
	if (text_width(s) % 2 != 1) {
	    fprintf(stderr, "Warning: Leftmost time's text is of even width. Label may be misaligned.\n");
	    fprintf(stderr, "         Check axes.c::draw_time_axis()\n");
	}
	if (x < min_x + text_width(s)/2) x = min_x + text_width(s)/2;
	draw_text(s, x, 1, CENTER, BOTTOM);
    }

    /* To */
    if (DELTA_LE(max_time, audio_files_length())) {
	draw_text(seconds_to_string(max_time), max_x, 1, RIGHT, BOTTOM);
    } else {
    	/* Draw max_time wherever it is on-screen.
	 * We mark the start time of each column we label,
	 * so truncate end time to the stride.
	 */
	double column_start_time = trunc(audio_files_length() / secpp) * secpp;
	int x = time_to_screen_column(column_start_time);
	char *s = seconds_to_string(column_start_time);

	/*
	 * We center it on the rightmost column but stop the text
	 * overflowing the right edge into the note name axis.
	 * This simple formula works for odd values of text_width(s), and
	 * numeric strings always have odd width at present, as for "0.00"
	 * above.
	 */
	if (text_width(s) % 2 != 1) {
	    fprintf(stderr, "Warning: Rightmost time's text is of even width. Label may be misaligned.\n");
	    fprintf(stderr, "         Check axes.c::draw_time_axis()\n");
	}
	if (x + text_width(s)/2 > max_x)
	    x = max_x - text_width(s)/2;
	draw_text(s, x, 1, CENTER, BOTTOM);
    }

    gui_unlock();

    gui_update_rect(min_x, 0, max_x, min_y - 1);
}
