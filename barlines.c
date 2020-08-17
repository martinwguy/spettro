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
 * barlines.c - Overlay the graphic with pixel-wide vertical limes to help
 * figure out the rhythm of a piece.
 *
 * If you press 2, 3, 4 ecc, this makes the bar lines three pixels wide and
 * gives N-1 one-pixel-wide beat lines (think: 4 to a bar).
 * 1 reverts to no beat lines with one-pixel-wide bar lines again
 * and 0 reverts to no bar lines either.
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

#include "axes.h"
#include "convert.h"
#include "gui.h"
#include "paint.h"
#include "ui.h"

/* Helper function tells whether to display a bar line at this pixel offset */
static int is_bar_line(int x);
/* and its return values, other than FALSE */
#define BAR_LINE 1
#define BEAT_LINE 2

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

void
set_beats_per_bar(int new_bpb)
{
    int old_bpb = beats_per_bar;

    if (left_bar_time != UNDEFINED && right_bar_time != UNDEFINED) {
	int col;
	/* Clear/repaint existing beat lines */
	for (col=min_x; col <= max_x; col++) {
	    /* If there was a beat line here with the old settings,
	     * repaint that column as it will be with the new settings */
	    switch (is_bar_line(col)) {
	    case BAR_LINE:
	    	/* When turning beat lines off, the bar lines need to shrink
		 * from 3 pixels wide to 1.
		 * The test is "skip if not turning them off" */
		if (!(old_bpb > 1 && new_bpb < 2)) break;
		/* otherwise drop through... */
	    case BEAT_LINE:
		beats_per_bar = new_bpb;
		repaint_column(col, min_y, max_y, FALSE);
		gui_update_column(col);
		beats_per_bar = old_bpb;
		break;
	    }
	}

	/* Paint the newly-appeared beat lines. This could be quicker
	 * not repainting the bar lines, but only the beat lines.
	 */
	beats_per_bar = new_bpb;

	for (col=min_x; col <= max_x; col++) {
	    switch (is_bar_line(col)) {
	    case BAR_LINE:
		/* When turning beat lines on, the bar lines need to grow to
		 * three pixels wide. Test: if not turning them on, skip. */
	    	if (!(old_bpb < 2 && new_bpb > 1)) break;
	    case BEAT_LINE:
		repaint_column(col, min_y, max_y, FALSE);
		gui_update_column(col);
	    }
	}
    }

    beats_per_bar = new_bpb;
}

static void
set_bar_time(double *this_one, double *the_other_one, double when)
{
    /* If only this bar line is defined, show it */
    if (*the_other_one == UNDEFINED) {
        int new_col;
	/* Move the sole marker */
	if (*this_one != UNDEFINED) {
	    int old_col = time_to_screen_column(*this_one);
	    *this_one = when;
	    if (old_col >= min_x && old_col <= max_x) {
		repaint_column(old_col, min_y, max_y, FALSE);
		gui_update_column(old_col);
	    }
	}
	*this_one = when;
	new_col = time_to_screen_column(when);
	repaint_column(new_col, min_y, max_y, FALSE);
	gui_update_column(new_col);

	goto out;
    }

    /* From here on, we know that *the_other_one is not UNDEFINED */

    /* If both were already defined, clear existing bar lines */
    if (*this_one != UNDEFINED) {
	double old_this_one = *this_one;
	int col;
	for (col=min_x; col <= max_x; col++) {
	    /* If there was a bar line here with the old settings,
	     * repaint that column as it will be with the new settings */
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

    /* If they define both bar lines at the same column, we just draw one
     * bar line and no beat lines
     */
    if (time_to_piece_column(left_bar_time) ==
	time_to_piece_column(right_bar_time)) {
	int col = time_to_screen_column(left_bar_time);

	*this_one = when;

	/* If the column is on-screen, repaint it */
	if (col >= min_x && col <= max_x) {
	    repaint_column(col, min_y, max_y, FALSE);
	    gui_update_column(col);
	}
    } else {
	/* Both are defined at different times. Paint the new bar lines. */
	int col;
	for (col=min_x; col <= max_x; col++) {
	    if (is_bar_line(col)) {
		repaint_column(col, min_y, max_y, FALSE);
		gui_update_column(col);
	    }
	}
    }
out:
    if (show_time_axes) draw_time_axis();
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
    bool is_overlaid = FALSE;
    color_t color;

    /* Bar lines take priority over the green line so that they don't
     * appear to flash as they cross it while playing and so that you can
     * see when you have placed a bar line at the current playing position
     * when it's paused. */
    if (is_bar_line(x)) {
	color = white; is_overlaid = TRUE; 
    } else
    if (x == disp_offset && !green_line_off) {
	/* If you want a pixel-wide green line, use here
	 * color = green; is_overlaid = TRUE;
	 *
	 * Instead, we say it's not overlaid so that we can OR the green line
	 * into it when it's painted (unless it's covered by a bar line,
	 * of course, which was handled above.) */
	is_overlaid = FALSE;
    }

    if (is_overlaid && colorp != NULL) *colorp = color;

    return is_overlaid;
}

/* Does screen column x coincide with the position of a bar or beat line?
 *
 * This is where bar lines are made three pixels wide when beat lines are shown,
 * by answering "yes" if either of the adjacent columns is on a bar line.
 *
 * Returns:
 * BAR_LINE (1) if this is the position of a bar line or,
 *		if beat lines are being displayed, a bar line or one of the
 *		adjecent pixel columns (to make the bar lines 3 pixels wide)
 * BEAT_LINE (2) if there is one of the beats at this position
 * FALSE (0)	if there is neither at this column.
 */
static int
is_bar_line(int pos_x)
{
    int x;		/* Column index into the whole piece */

    /* The bar positions in pixel columns since the start of the piece. */
    int left_bar_ticks = -disp_width;  /* impossible value, surely off-screen */
    int right_bar_ticks = -disp_width;
    int bar_width = 0;	/* How long is the bar in pixels? 0: there is no bar */

    /* If neither of the bar positions is defined, there are none displayed */
    if (left_bar_time == UNDEFINED &&
	right_bar_time == UNDEFINED) return FALSE;

    x = time_to_piece_column(screen_column_to_start_time(pos_x));

    if (left_bar_time != UNDEFINED)
	left_bar_ticks = time_to_piece_column(left_bar_time);
    if (right_bar_time != UNDEFINED)
	right_bar_ticks = time_to_piece_column(right_bar_time);

    if (left_bar_time != UNDEFINED && right_bar_time != UNDEFINED) {
	bar_width = right_bar_ticks - left_bar_ticks;
	if (bar_width < 0) {
	    /* left bar line is right of right bar line */
	    bar_width = -bar_width;
	}
    }

    /* If only one of the bar positions is defined, only that one is displayed.
     * Idem if they've defined both bar lines in the same pixel column.
     *
     * Both UNDEFINED is handled above; if either are UNDEFINED here,
     * bar_*_ticks will not be called.
     */
    if (left_bar_time == UNDEFINED ||
	right_bar_time == UNDEFINED ||
	bar_width == 0) {

	if (x == left_bar_ticks || x == right_bar_ticks) return BAR_LINE;
	return FALSE;
    }

    /* Both bar positions are defined. See if this column falls on one. */

    /* Left_bar_ticks can be negative if they mouse drag the bar line
     * into the gray area before the graphic. Make the "% bar_width" checks
     * below work anyway - it's not used for anything else.
     */
    if (left_bar_ticks < 0)
	left_bar_ticks += (disp_width / bar_width) * bar_width;

    if (beats_per_bar < 2)
	return x % bar_width == left_bar_ticks % bar_width;
    else if (x % bar_width == left_bar_ticks % bar_width)
	/* It falls on a bar line */
	return BAR_LINE;
    else if (beats_per_bar > 1 &&
	     ((x-1) % bar_width == left_bar_ticks % bar_width ||
	      (x+1) % bar_width == left_bar_ticks % bar_width))
       /* If beats are shown, make the bar lines three pixels wide */
       return BAR_LINE;
    else if (beats_per_bar > 1) {
	/* See if any beat's time falls within the time covered by this column.
	 * This is true if left_bar_time + N * beat step is within .5*secpp of
	 * the time represented by the center of the column.
	 * Convert the time represented by the center of the column to the
	 * nearest beat, and if it's within secpp/2 of the center, say yes.
	 */
        double column_center_time = x * secpp + secpp/2;
        double beat_period = fabs(right_bar_time - left_bar_time) / beats_per_bar;
	double nearest_beat = round((column_center_time - left_bar_time) / beat_period) * beat_period + left_bar_time;

	/* Each column represents from its start time up to but not
	 * including the following one
	 */
	if (nearest_beat < column_center_time) {
	    if (DELTA_LE(column_center_time - nearest_beat, secpp/2))
		return BEAT_LINE;
	} else {
	    if (DELTA_LT(nearest_beat - column_center_time, secpp/2))
		return BEAT_LINE;
	}
    }
    return FALSE;
}
