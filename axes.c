/* axes.c: Stuff to draw axes at the edges of the graphic */

#include "spettro.h"
#include "axes.h"

#include "main.h"
#include "gui.h"
#include "text.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

/* The greatest number of linear ticks seems to occurs from 0-14000 (15 ticks).
** The greatest number of log ticks occurs 10-99999 or 11-100000 (35 ticks).
** Search for "worst case" for the commentary below that says why it is 35.
*/
typedef struct
{	/* [35] or more */
	double value [40] ;	/* Number to print, or NO_NUMBER */
	double distance [40] ;
	/* The digit that changes from label to label.
	** This ensures that a range from 999 to 1001 prints 999.5 and 1000.5
	** instead of 999 1000 1000 1000 1001.
	*/
	int decimal_places_to_print ;
} TICKS;

/* Value to store in the ticks.value[k] field to mean
** "Put a tick here, but don't print a number."
** NaN (0.0/0.0) is untestable without isnan() so use a random value.
*/
#define NO_NUMBER (M_PI)		/* They're unlikely to hit that! */

/* Is this entry in "ticks" one of the numberless ticks? */
#define JUST_A_TICK(ticks, k)	(ticks.value [k] == NO_NUMBER)

/* Forward declarations */

static int calculate_ticks(double min, double max, double distance, int log_scale, TICKS *ticks);
static int calculate_log_ticks(double min, double max, double distance, TICKS *ticks);

void
draw_frequency_axis(void)
{
    TICKS ticks;

    /* Put ticks on Frequency axis */
    int tick_count = calculate_ticks (min_freq, max_freq, max_y - min_y, 1, &ticks) ;
    int i;

    gui_paint_rect(0, 0, min_x-1,  disp_height-1, black);

    for (i=0; i < tick_count; i++) {
	char s[6];
	gui_putpixel(min_x-1, min_y + lrint(ticks.distance[i]), (char *)&green);
	gui_putpixel(min_x-2, min_y + lrint(ticks.distance[i]), (char *)&green);
	if (ticks.value[i] != NO_NUMBER) {
	    char *spacep;
	    sprintf(s, "%-5g", ticks.value[i]);
	    if ((spacep = strchr(s, ' ')) != NULL) *spacep = '\0';
	    draw_text(s,
		      min_x-4, min_y+lrint(ticks.distance[i]),
		      RIGHT, CENTER);
	}
    }
}

/* Decide where to put ticks and numbers on an axis.
**
** Graph-labelling convention is that the least significant digit that changes
** from one label to the next should change by 1, 2 or 5, so we step by the
** largest suitable value of 10^n * {1, 2 or 5} that gives us the required
** number of divisions / numeric labels.
*/

/* The old code used to make 6 to 14 divisions and number every other tick.
** What we now mean by "division" is one of teh gaps between numbered segments
** so we ask for a minimum of 3 to give the same effect as the old minimum of
** 6 half-divisions.
** This results in the same axis labelling for all maximum values
** from 0 to 12000 in steps of 1000 and gives sensible results from 13000 on,
** to a maximum of 7 divisions and 8 labels from 0 to 14000.
**/
#define TARGET_DIVISIONS 3

/* log_scale is pseudo-boolean:
** 0 means use a linear scale,
** 1 means use a log scale and
** 2 is an internal value used when calling back from calculate_log_ticks() to
**   label the range with linear numbering but logarithmic spacing.
*/
static int
calculate_ticks (double min, double max, double distance, int log_scale, TICKS * ticks)
{
	double step ;	/* Put numbered ticks at multiples of this */
	double range = max - min ;
	int k ;
	double value ;	/* Temporary */

	if (log_scale == 1)
		return calculate_log_ticks (min, max, distance, ticks) ;

	/* Linear version */

	/* Choose a step between successive axis labels so that one digit
	** changes by 1, 2 or 5 amd that gives us at least the number of
	** divisions (and numberic labels) that we would like to have.
	**
	** We do this by starting "step" at the lowest power of ten <= max,
	** which can give us at most 9 divisions (e.g. from 0 to 9999, step 1000)
	** Then try 5*this, 2*this and 1*this.
	*/
	step = pow (10.0, floor (log10 (max))) ;
	do
	{	if (range / (step * 5) >= TARGET_DIVISIONS)
		{	step *= 5 ;
			break ;
			} ;
		if (range / (step * 2) >= TARGET_DIVISIONS)
		{	step *= 2 ;
			break ;
			} ;
		if (range / step >= TARGET_DIVISIONS)
			break ;
		step /= 10 ;
	} while (1) ;	/* This is an odd loop! */

	/* Ensure that the least significant digit that changes gets printed, */
	ticks->decimal_places_to_print = lrint (-floor (log10 (step))) ;
	if (ticks->decimal_places_to_print < 0)
		ticks->decimal_places_to_print = 0 ;

	/* Now go from the first multiple of step that's >= min to
	 * the last one that's <= max. */
	k = 0 ;
	value = ceil (min / step) * step ;

#define add_tick(val, just_a_tick) do \
	{	if (val >= min - DELTA && val < max + DELTA) \
		{	ticks->value [k] = just_a_tick ? NO_NUMBER : val ; \
			ticks->distance [k] = distance * \
				(log_scale == 2 \
					? /*log*/ (log (val) - log (min)) / (log (max) - log (min)) \
					: /*lin*/ (val - min) / range) ; \
			k++ ; \
			} ; \
		} while (0)

	/* Add the half-way tick before the first number if it's in range */
	add_tick (value - step / 2, TRUE) ;

	while (value <= max + DELTA)
	{ 	/* Add a tick next to each printed number */
		add_tick (value, FALSE) ;

		/* and at the half-way tick after the number if it's in range */
		add_tick (value + step / 2, TRUE) ;

		value += step ;
		} ;

	return k ;
} /* calculate_ticks */

/* Number/tick placer for logarithmic scales.
**
** Some say we should number 1, 10, 100, 1000, 1000 and place ticks at
** 2,3,4,5,6,7,8,9, 20,30,40,50,60,70,80,90, 200,300,400,500,600,700,800,900
** Others suggest numbering 1,2,5, 10,20,50, 100,200,500.
**
** Ticking 1-9 is visually distinctive and emphasizes that we are using
** a log scale, as well as mimicking log graph paper.
** Numbering the powers of ten and, if that doesn't give enough labels,
** numbering also the 2 and 5 multiples might work.
**
** Apart from our [number] and tick styles:
** [1] 2 5 [10] 20 50 [100]  and
** [1] [2] 3 4 [5] 6 7 8 9 [10]
** the following are also seen in use:
** [1] [2] 3 4 [5] 6 7 [8] 9 [10]  and
** [1] [2] [3] [4] [5] [6] 7 [8] 9 [10]
** in https://www.lhup.edu/~dsimanek/scenario/errorman/graphs2.htm
**
** This works fine for wide ranges, not so well for narrow ranges like
** 5000-6000, so for ranges less than a decade we apply the above
** linear numbering style 0.2 0.4 0.6 0.8 or whatever, but calulating
** the positions of the legends logarithmically.
**
** Alternatives could be:
** - by powers or two from some starting frequency
**   defaulting to the Nyquist frequency (22050, 11025, 5512.5 ...) or from some
**   musical pitch (220, 440, 880, 1760)
** - with a musical note scale  C0 ' D0 ' E0 F0 ' G0 ' A0 ' B0 C1
** - with manuscript staff lines, piano note or guitar string overlay.
*/

/* Helper functions: add ticks and labels at start_value and all powers of ten
** times it that are in the min-max range.
** This is used to plonk ticks at 1, 10, 100, 1000 then at 2, 20, 200, 2000
** then at 5, 50, 500, 5000 and so on.
*/
static int
add_log_ticks (double min, double max, double distance, TICKS * ticks,
				int k, double start_value, bool include_number)
{	double value ;

	for (value = start_value ; value <= max + DELTA ; value *= 10.0)
	{	if (value < min - DELTA) continue ;
		ticks->value [k] = include_number ? value : NO_NUMBER ;
		ticks->distance [k] = distance * (log (value) - log (min)) / (log (max) - log (min)) ;
		k++ ;
		} ;
	return k ;
} /* add_log_ticks */

static int
calculate_log_ticks (double min, double max, double distance, TICKS * ticks)
{	int k = 0 ;	/* Number of ticks we have placed in "ticks" array */
	double underpinning ; 	/* Largest power of ten that is <= min */

	/* If the interval is less than a decade, just apply the same
	** numbering-choosing scheme as used with linear axis, with the
	** ticks positioned logarithmically.
	*/
	if (max / min < 10.0)
		return calculate_ticks (min, max, distance, 2, ticks) ;

	/* If the range is greater than 1 to 1000000, it will generate more than
	** 19 ticks.  Better to fail explicitly than to overflow.
	*/
	if (max / min > 1000000)
	{	printf ("Error: Frequency range is too great for logarithmic scale.\n") ;
		exit (1) ;
		} ;

	/* First hack: label the powers of ten. */

 	/* Find largest power of ten that is <= minimum value */
	underpinning = pow (10.0, floor (log10 (min))) ;

	/* Go powering up by 10 from there, numbering as we go. */
	k = add_log_ticks (min, max, distance, ticks, k, underpinning, TRUE) ;

	/* Do we have enough numbers? If so, add numberless ticks at 2 and 5 */
	if (k >= TARGET_DIVISIONS + 1) /* Number of labels is n.of divisions + 1 */
	{
		k = add_log_ticks (min, max, distance, ticks, k, underpinning * 2.0, FALSE) ;
		k = add_log_ticks (min, max, distance, ticks, k, underpinning * 5.0, FALSE) ;
		}
	else
	{	int i ;
		/* Not enough numbers: add numbered ticks at 2 and 5 and
		 * unnumbered ticks at all the rest */
		for (i = 2 ; i <= 9 ; i++)
			k = add_log_ticks (min, max, distance, ticks, k,
								underpinning * (1.0 * i), i == 2 || i == 5) ;
		} ;

	/* Greatest possible number of ticks calculation:
	** The worst case is when the else clause adds 8 ticks with the maximal
	** number of divisions, which is when k == TARGET_DIVISIONS, 3,
	** for example 100, 1000, 10000.
	** The else clause adds another 8 ticks inside each division as well as
	** up to 8 ticks after the last number (from 20000 to 90000)
	** and 8 before to the first (from 20 to 90 in the example).
	** Maximum possible ticks is 3+8+8+8+8=35
	*/

	return k ;
} /* calculate_log_ticks */
