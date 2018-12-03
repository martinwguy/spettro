/*
 * paint.c - Device-independent routines to paint the graphic display
 */

#include "spettro.h"
#include "paint.h"

#include "audio.h"
#include "barlines.h"
#include "cache.h"
#include "colormap.h"
#include "gui.h"
#include "interpolate.h"
#include "overlay.h"
#include "scheduler.h"
#include "timer.h"	/* for scroll_event_pending */
#include "ui.h"

/* Helper function */
static void calc_columns(int from, int to);

/*
 * Really scroll the screen
 */
void
do_scroll()
{
    double new_disp_time;	/* Where we reposition to */
    int scroll_by;		/* How many pixels to scroll by.
				 * +ve = move forward in time, move display left
				 * +ve = move back in time, move display right
				 */
    bool scroll_forward;	/* Normal forward scroll, moving the graph left? */

    scroll_event_pending = FALSE;

    new_disp_time = get_playing_time();

    if (DELTA_LE(new_disp_time, 0.0))
	new_disp_time = 0.0;
    if (DELTA_GE(new_disp_time, audio_file_length(audio_file)))
	new_disp_time = audio_file_length(audio_file);

    /* Align to a multiple of 1/ppsec so that times in cached results
     * continue to match. This is usually a no-op.
     */
    new_disp_time = floor(new_disp_time / step + DELTA) * step;

    scroll_by = lrint((new_disp_time - disp_time) * ppsec);

    /*
     * Scroll the display sideways by the correct number of pixels
     * and start a calc thread for the newly-revealed region.
     *
     * (4 * scroll_by) is the number of bytes by which we scroll.
     * The right-hand columns will fill with garbage or the start of
     * the next pixel row, and the final "- (4*scroll_by)" is so as
     * not to scroll garbage from past the end of the frame buffer.
     */
    if (scroll_by == 0) return;

    if (abs(scroll_by) >= max_x - min_x + 1) {
	/* If we're scrolling by more than the display width, repaint it all */
	disp_time = new_disp_time;
	repaint_display(FALSE);
	return;
    }

    scroll_forward = (scroll_by > 0);
    if (scroll_by < 0) scroll_by = -scroll_by;

    /* Otherwise, shift the overlapping region and calculate the new */

    /*
     * If the green line will remain on the screen,
     * replace it with spectrogram data.
     * There are disp_offset columns left of the line.
     * If there is no result for the line, schedule its calculation
     * as it will need to be repainted when it has scrolled.
     * If logmax has changed since the column was originally painted,
     * it is repainted at a different brightness, so repaint
     */
    if (scroll_by <= scroll_forward ? (disp_offset - min_x)
				    : (max_x - disp_offset - 1) ) {
	green_line_off = TRUE;
	repaint_column(disp_offset, min_y, max_y, FALSE);
	green_line_off = FALSE;
    }

    disp_time = new_disp_time;

    gui_h_scroll_by(scroll_forward ? scroll_by : -scroll_by);

    repaint_column(disp_offset, min_y, max_y, FALSE);

    if (scroll_forward) {
	/* Repaint the right edge */
	int x;
	for (x = max_x - scroll_by; x <= max_x; x++) {
	    repaint_column(x, min_y, max_y, FALSE);
	}
    } else {
	/* Repaint the left edge */
	int x;
	for (x = min_x + scroll_by - 1; x >= min_x; x--) {
	    repaint_column(x, min_y, max_y, FALSE);
	}
    }

    /* The whole screen has changed (well, unless there's background) */
    gui_update_display();
}

/* Repaint the display.
 *
 * If "refresh_only" is FALSE, repaint every column of the display
 * from the result cache or paint it with the background color
 * if it hasn't been calculated yet (and ask for it to be calculated)
 * or is before/after the start/end of the piece.
 *
 * If "refresh_only" if TRUE, repaint only the columns that are already
 * displaying spectral data.
 *   This is used when something changes that affects their appearance
 * retrospectively, like logmax or dyn_range changing, or vertical scrolling,
 * where there's no need to repaint background, bar lines or the green line.
 *   Rather than remember what has been displayed, we repaint on-screen columns
 * that are in the result cache and have had their magnitude spectrum calculated
 * (the conversion from linear to log and the colour assignment happens when
 * an incoming result is processed to be displayed).
 *
 * Returns TRUE if the result was found in the cache and repainted,
 *	   FALSE if it painted the background color or was off-limits.
 * The GUI screen-updating function is called by whoever called us.
 */

void
repaint_display(bool refresh_only)
{
    repaint_columns(min_x, max_x, min_y, max_y, refresh_only);

    gui_update_display();
}

void
repaint_columns(int from_x, int to_x, int from_y, int to_y, bool refresh_only)
{
    int x;

    for (x=from_x; x <= to_x; x++) {
	if (refresh_only) {
	    /* Don't repaint bar lines or the green line */
	    if (get_col_overlay(x, NULL)) continue;
	}
	repaint_column(x, min_y, max_y, refresh_only);
    }
}

/* Repaint a column of the display from the result cache or paint it
 * with the background color if it hasn't been calculated yet or with the
 * bar lines.
 *
 * from_y and to_y limit the repainting to just the specified rows
 * (0 and disp_height-1 to paint the whole column).
 *
 * if "refresh_only" is TRUE, we only repaint columns that are already
 * displaying spectral data; we find out if a column is displaying spectral data
 * by checking the result cache: if we have a result for that time/fft_freq,
 * it's probably displaying something.
 *
 * and we don't schedule the calculation of columns whose spectral data
 * has not been displayed yet.
 *
 * The GUI screen-updating function is called by whoever called us.
 */
void
repaint_column(int column, int from_y, int to_y, bool refresh_only)
{
    /* What time does this column represent? */
    double t = disp_time + (column - disp_offset) * step;
    result_t *r;

    if (column < min_x || column > max_x) {
	fprintf(stderr, "Repainting off-screen column %d\n", column);
	abort();
	return;
    }

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */

    /* If the column is before/after the start/end of the piece,
     * give it the background colour */
    if (DELTA_LT(t, 0.0) || DELTA_GT(t, audio_file_length(audio_file))) {
	if (!refresh_only)
	    gui_paint_column(column, min_y, max_y, background);
	return;
    }

    if (refresh_only) {
	/* If there's a bar line or green line here, nothing to do */
	if (get_col_overlay(column, NULL)) return;

	/* If there's any result for this column in the cache, it should be
	 * displaying something, but it might be for the wrong speclen/window.
	 * We have no way of knowing what it is displaying so force its repaint
	 * with the current parameters.
	 */
	if ((r = recall_result(t, -1, -1)) != NULL) {
	    /* There's data for this column. */
	    if (r->speclen == speclen && r->window == window_function) {
		/* Bingo! It's the right result */
		paint_column(column, from_y, to_y, r);
	    } else {
		/* Bummer! It's for something else. Repaint it. */
		repaint_column(column, from_y, to_y, FALSE);
	    }
	} else {
	    /* There are no results in-cache for this column,
	     * so it can't be displaying any spectral data */
	}
    } else {
	color_t ov;
	if (get_col_overlay(column, &ov)) {
	    gui_paint_column(column, from_y, to_y, ov);
	} else
	/* If we have the right spectral data for this column, repaint it */
	if ((r = recall_result(t, speclen, window_function)) != NULL) {
	    paint_column(column, from_y, to_y, r);
	} else {
	    /* ...otherwise paint it with the background color */
	    gui_paint_column(column, from_y, to_y, background);

	    /* and if it was for a valid time, schedule its calculation */
	    if (DELTA_GE(t, 0.0) && DELTA_LE(t, audio_file_length(audio_file))) {
		calc_columns(column, column);
	    }
	}
    }
}

/* Paint a column for which we have result data.
 * pos_x is a screen coordinate.
 * min_y and max_y limit the updating to those screen rows.
 * The GUI screen-updating function is called by whoever called us.
 */
void
paint_column(int pos_x, int from_y, int to_y, result_t *result)
{
    float *logmag;
    double logmax;	/* maximum log magnitude seen so far */
    int y;
    color_t ov;		/* Overlay color */

    /*
     * Apply column overlay
     */
    if (get_col_overlay(pos_x, &ov)) {
	gui_paint_column(pos_x, from_y, to_y, ov);
	return;
    }

    assert(maglen == max_y - min_y + 1);
    logmag = Calloc(maglen, sizeof(*logmag));
    logmax = interpolate(logmag, result->spec, from_y, to_y);

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    gui_lock();		/* Allow pixel-writing access */
    for (y=from_y; y <= to_y; y++) {
        int k = y - min_y;
	/* Apply row overlay, if any, otherwise paint the pixel */
	gui_putpixel(pos_x, y,
		     get_row_overlay(y, &ov)
		     ? ov :
		     /* OR in the green line if it's on */
		     ((!green_line_off && pos_x == disp_offset) ? green : 0)
		     |
		     colormap(20.0 * (logmag[k] - logmax), min_db));
    }
    gui_unlock();

    free(logmag);

    /* If the maximum amplitude changed, we should repaint the already-drawn
     * columns at the new brightness. We tried this calling repaint_display here
     * but, apart from causing a jumpy pause in the scrolling, there was worse:
     * each time logmax increased it would schedule the same columns a dozen
     * times, resulting in the same calculations being done several times and
     * the duplicate results being thrown away. The old behaviour of reshading
     * the individual columns as they pass the green line is less bad.
     */
}

#define max(a, b) ((a)>(b) ? (a) : (b))
#define min(a, b) ((a)<(b) ? (a) : (b))

/*
 * Schedule the FFT thread(s) to calculate the results for these display columns
 */
static void
calc_columns(int from_col, int to_col)
{
    calc_t *calc;
    /* The times represented by from_col and to_col */
    double from = disp_time + (from_col - disp_offset) * step;
    double to   = disp_time + (to_col - disp_offset) * step;;

    calc = Malloc(sizeof(calc_t));
    calc->audio_file = audio_file;
    calc->speclen= speclen;
    calc->window = window_function;

    /*
     * Limit the range to the start and end of the audio file.
     */
    if (DELTA_LE(from, 0.0)) from = 0.0;
    if (DELTA_LE(to, 0.0)) to = 0.0;
    {
	/* End of audio file as a multiple of step */
	double last_time= floor(audio_file_length(audio_file) / step) * step;

	if (DELTA_GE(from, last_time))	from = last_time;
	if (DELTA_GE(to, last_time))	to = last_time;
    }

    /* If it's for a single column, just schedule it... */
    if (from_col == to_col) {
	calc->t = from;
	schedule(calc);
    } else {
	/* ...otherwise, schedule each column from "from" to "to" individually
	 * in the same order as get_work() will choose them to be calculated.
	 * If we were to schedule them in time order, and some of them are
	 * left of disp_time, then a thread calling get_work() before we have
	 * finished scheduling them would calculate and display a lone column
	 * in the left pane.
	 */

	/* Allow for descending ranges by putting "from" and "to"
	 * into ascending order */
	if (to < from) {
	    double tmp = from;
	    from = to;
	    to = tmp;
	}
	/* get_work() does first disp_time to right edge,
	 * then disp_time-1 to left edge.
	 */
	/* Columns >= disp_time */
	if (DELTA_GE(to, disp_time)) {
	    double t;
	    for (t = max(from, disp_time); DELTA_LE(t, to); t += step) {
		calc_t *new = Malloc(sizeof(calc_t));
		memcpy(new, calc, sizeof(calc_t));
		new->t = t;
		schedule(new);
	    }
	}
	/* Do any columns that are < disp_time in reverse order */
	if (DELTA_LT(from, disp_time)) {
	    double t;
	    for (t=disp_time - step; DELTA_GE(t, from); t -= step) {
		calc_t *new = Malloc(sizeof(calc_t));
		memcpy(new, calc, sizeof(calc_t));
		new->t = t;
		schedule(new);
	    }
	}
	/* For ranges, all calc_t's we scheduled were copies of calc */
	free(calc);
    }
}
