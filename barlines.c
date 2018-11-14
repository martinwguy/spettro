/*
 * barlines.c - Overlay the graphic with pixel-wide vertical limes to help
 * figure out the rhythm of a piece.
 *
 * One day the bar lines will be 3 pixels wide with intermediate beat markers
 * 1 pixel wide.
 *
 * The column overlay takes priority over the row overlay, so that
 * "bar lines" are maintained whole, not cut, and the bar lines overlay the
 * green line to avoid having them flash as they cross it while playing.
 *
 * The column overlays depend on the start and end of a bar,
 * measured in pixels, not time, for convenience.
 * If a beat line doesn't fall exactly on a pixel's timestamp, we round
 * it to the nearest pixel.
 */

#include "spettro.h"
#include "barlines.h"

#include "gui.h"
#include "main.h"

#include <math.h>

/* Helper function tells whether to display a bar line at this pixel offset */
static bool is_bar_line(int x);

/* Markers for the start and end of one bar, measured in seconds from
 * the start of the piece.
 */
#define UNDEFINED (-1.0)
static double left_bar_time = UNDEFINED;
static double right_bar_time = UNDEFINED;

/* The same bar positions converted to a pixel index measured from 
 * the start of the piece.
 */
#define left_bar_ticks (lrint(left_bar_time / step))
#define right_bar_ticks (lrint(right_bar_time / step))

/* Set start and end of marked bar.
 * If neither is defined, we display nothing.
 * If only one is defined, display a marker at that point.
 * If both are defined at the same time, cancel both.
 * If both are defined, we display both and other barlines at the same interval.
 *
 * For speed, when bar line positions change, we wipe out the already-displayed
 * ones and redraw the new ones.
 */

static void set_bar_time(double *this_one, double *the_other_one, double when);

void
set_left_bar_time(double when)
{
    set_bar_time(&left_bar_time, &right_bar_time, when);
}

void
set_right_bar_time(double when)
{
    set_bar_time(&right_bar_time, &left_bar_time, when);
}

static void
set_bar_time(double *this_one, double *the_other_one, double when)
{
    if (*the_other_one == UNDEFINED) {
        int new_col;
	/* Move the sole left marker */
	if (*this_one != UNDEFINED) {
	    int old_col = disp_offset + lrint((*this_one - disp_time) / step);
	    *this_one = when;
	    if (old_col >= min_x && old_col <= max_x) {
		repaint_column(old_col, min_y, max_y, FALSE);
		gui_update_column(old_col);
	    }
	}
	*this_one = when;
	new_col = disp_offset + floor((when - disp_time) / step);
	repaint_column(new_col, min_y, max_y, FALSE);
	gui_update_column(new_col);

	return;
    }

    /* If both were already defined, clear existing bar lines */
    if (*this_one != UNDEFINED) {
	double old_this_one = *this_one;
	int col;
	for (col=min_x; col <= max_x; col++) {
	    if (is_bar_line(col)) {	
		*this_one = when;
		repaint_column(col, min_y, max_y, FALSE);
		gui_update_column(col);
		*this_one = old_this_one;
	    }
	}
    }

    /* Set this bar marker's position */
    *this_one = when;

    /* Defining both bar lines at the same time is how you remove them */
    if (left_bar_ticks == right_bar_ticks) {
	left_bar_time = right_bar_time = UNDEFINED;
	return;
    }

    /* Both are defined at different times. Paint the new bar lines. */
    {   int col;
	for (col=min_x; col <= max_x; col++) {
	    if (is_bar_line(col)) {
		repaint_column(col, min_y, max_y, FALSE);
		gui_update_column(col);
	    }
	}
    }
}

/*
 * What colour overlays this screen column?
 * 
 * Returns TRUE if there is a column overlay, FALSE if not and writes
 * the color into what colorp points at if it's not NULL.
 */
bool
get_col_overlay(int x, color_t *colorp)
{
    bool is_overlayed = FALSE;
    color_t color;

    /* Bar lines take priority over the green line so that they don't
     * appear to flash as they cross it while playing and so that you can
     * see when you have placed a bar line at the current playing position
     * when it's paused. */
    if (is_bar_line(x)) {
	color = white; is_overlayed = TRUE; 
    } else
    if (x == disp_offset && !green_line_off) {
	color = green; is_overlayed = TRUE;
    }

    if (is_overlayed && colorp != NULL) *colorp = color;

    return is_overlayed;
}

/* Does screen column x coincide with the position of a bar line? */
static bool
is_bar_line(int x)
{
    int bar_width;	/* How long is the bar in pixels? */

    /* Convert screen-x to column index into the whole piece */
    x += lrint(disp_time / step) - disp_offset;

    /* If neither of the bar positions is defined, there are none displayed */
    if (left_bar_time == UNDEFINED &&
	right_bar_time == UNDEFINED) return FALSE;

    bar_width = right_bar_ticks - left_bar_ticks;
    /* They can set the "left" and "right" bar lines the other way round too */
    if (bar_width < 0) bar_width = -bar_width;

    /* If only one of the bar positions is defined, only that one is displayed.
     * Idem if they've defined both bar lines in the same pixel column.
     *
     * Both UNDEFINED is handled above; if either are UNDEFINED here,
     * bar_*_ticks will not be called.
     */
    if (left_bar_time == UNDEFINED ||
	right_bar_time == UNDEFINED ||
	left_bar_ticks == right_bar_ticks) {

	return x == left_bar_ticks || x == right_bar_ticks;
    }

    /* Both bar positions are defined. See if this column falls on one. */
    return x % bar_width == left_bar_ticks % bar_width;
}
