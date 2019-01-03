/* axes.c: Stuff to draw axes at the edges of the graphic */

#include "spettro.h"
#include "axes.h"

#include "convert.h"
#include "gui.h"
#include "text.h"
#include "ui.h"

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
static void draw_frequency_axis(void);
static void draw_note_names(void);

void
draw_frequency_axes(void)
{
    draw_frequency_axis();
    draw_note_names();
}

/* Put ticks on Frequency axis. Only called when show_axes is TRUE. */
static void
draw_frequency_axis()
{
    int tick_count = calculate_ticks(min_freq, max_freq, max_y - min_y, 1);
    int i;

    gui_paint_rect(0, 0, min_x-1,  disp_height-1, black);

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
		draw_frequency_axis();
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

    gui_paint_rect(max_x, 0, disp_width - 1,  disp_height - 1, black);

    note_name[2] = '\0';
    gui_lock();
    for (note_name[0] = 'A'; note_name[0] <= 'G'; note_name[0]++) {
	for (note_name[1] = '0'; note_name[1] <= '9'; note_name[1]++) {
	    double freq = note_name_to_freq(note_name);
	    if (DELTA_GE(freq, min_freq) && DELTA_LE(freq, max_freq)) {
		int y = min_y + freq_to_magindex(freq);
		gui_putpixel(max_x + 1, y, green);
		gui_putpixel(max_x + 2, y, green);
		draw_text(note_name, max_x + 4, y, LEFT, CENTER);
	    }
	}
    }
    gui_unlock();
}

/* Decide where to put ticks and numbers on an axis.
 *
 * Graph-labelling convention is that the least significant digit that changes
 * from one label to the next should change by 1, 2 or 5, so we step by the
 * largest suitable value of 10^n * {1, 2 or 5} that gives us the required
 * number of divisions / numeric labels.
 */

/* The old code used to make 6 to 14 divisions and number every other tick.
 * What we now mean by "division" is one of teh gaps between numbered segments
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
    double step;	/* Put numbered ticks at multiples of this */
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
     * We do this by starting "step" at the lowest power of ten <= max,
     * which can give us at most 9 divisions (e.g. from 0 to 9999, step 1000)
     * Then try 5*this, 2*this and 1*this.
     */
    step = pow(10.0, floor(log10(max)));
    do {
	if (range / (step * 5) >= TARGET_DIVISIONS) {
	    step *= 5;
	    break;
	}
	if (range / (step * 2) >= TARGET_DIVISIONS) {
	    step *= 2;
	    break;
	}
	if (range / step >= TARGET_DIVISIONS) break;
	step /= 10;
    } while (1);	/* This is an odd loop! */

    /* Ensure that the least significant digit that changes gets printed, */
    decimal_places_to_print = lrint(-floor(log10(step)));
    if (decimal_places_to_print < 0)
	    decimal_places_to_print = 0;

    /* Now go from the first multiple of step that's >= min to
     * the last one that's <= max. */
    k = 0;
    value = ceil(min / step) * step;

    /* Add the half-way tick before the first number if it's in range */
    k = add_tick(k, min, max, distance, log_scale, value - step / 2, TRUE);

    while (DELTA_LE(value, max)) {
	/* Add a tick next to each printed number */
	k = add_tick(k, min, max, distance, log_scale, value, FALSE);

	/* and at the half-way tick after the number if it's in range */
	k = add_tick(k, min, max, distance, log_scale, value + step / 2, TRUE);

	value += step;
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

    for (value = start_value; DELTA_LE(value, max); value *= 10.0) {
	if (k >= number_of_ticks) {
	    ticks = Realloc(ticks, (k+1) * sizeof(*ticks));
	    number_of_ticks = k+1;
	}
	if (DELTA_LT(value, min)) continue;
	ticks[k].value = include_number ? value : NO_NUMBER;
	ticks[k].distance = distance * (log(value) - log(min))
				     / (log(max) - log(min));
	k++;
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
