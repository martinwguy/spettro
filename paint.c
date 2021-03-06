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
 * paint.c - Device-independent routines to paint the graphic display
 */

#include "spettro.h"
#include "paint.h"

#include "audio.h"
#include "audio_cache.h"
#include "axes.h"
#include "barlines.h"
#include "cache.h"
#include "convert.h"
#include "colormap.h"
#include "gui.h"
#include "interpolate.h"
#include "overlay.h"
#include "scheduler.h"
#include "timer.h"	/* for scroll_event_pending */
#include "ui.h"

/* Local function */
static void calc_column(int col);

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
    bool scroll_forward;	/* Forward scroll, moving the graph left? */

    scroll_event_pending = FALSE;

    new_disp_time = get_playing_time();

    if (DELTA_LE(new_disp_time, 0.0))
	new_disp_time = 0.0;
    if (DELTA_GE(new_disp_time, audio_file_length() - 1/current_sample_rate()))
	new_disp_time = audio_file_length() - 1/current_sample_rate();

    /* Align to a multiple of 1/ppsec so that times in cached results
     * continue to match. This is usually a no-op.
     */
    new_disp_time = round(new_disp_time / secpp) * secpp;

    scroll_by = time_to_piece_column(new_disp_time) -
		time_to_piece_column(disp_time);

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
	set_disp_time(new_disp_time);
	repaint_display(FALSE);
	if (show_time_axes) draw_time_axis();
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

    set_disp_time(new_disp_time);

    gui_h_scroll_by(scroll_forward ? scroll_by : -scroll_by);

    repaint_column(disp_offset, min_y, max_y, FALSE);

    if (scroll_forward) {
	/* Repaint the right edge */
	int x;
	for (x = max_x - scroll_by; x <= max_x + LOOKAHEAD; x++) {
	    repaint_column(x, min_y, max_y, FALSE);
	}
    } else {
	/* Scrolling left: precalculate a normal left-scroll's width. */
	int x;
	for (x = min_x + scroll_by - 1; x >= min_x - LOOKAHEAD; x--) {
	    repaint_column(x, min_y, max_y, FALSE);
	}
    }

    if (show_time_axes) draw_time_axis();

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
 */

void
repaint_display(bool refresh_only)
{
    /* repaint_display is what parameter-changing functions call to
     * repaint with the new parameters, so also recalculate the lookahead */
    repaint_columns(min_x - LOOKAHEAD, max_x + LOOKAHEAD, min_y, max_y, refresh_only);

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

    /* Limit GUI update to on-screen stuff, as we are also called to
     * precalculate off-screen columns.
     */
    if (from_x < min_x) from_x = min_x;
    if (to_x > max_x) to_x = max_x;
    gui_update_rect(from_x, from_y, to_x, to_y);
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
repaint_column(int pos_x, int from_y, int to_y, bool refresh_only)
{
    /* What time does this column represent? */
    double t = screen_column_to_start_time(pos_x);
    calc_t *r;

    if (pos_x < min_x - LOOKAHEAD || pos_x > max_x + LOOKAHEAD) {
	fprintf(stderr, "Repainting off-screen column %d\n", pos_x);
	return;
    }

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */

    /* If the column is before/after the start/end of the piece,
     * give it the background colour */
    if (DELTA_LT(t, 0.0) || DELTA_GT(t, audio_file_length())) {
	if (!refresh_only && pos_x >= min_x && pos_x <= max_x)
	    gui_paint_column(pos_x, min_y, max_y, background);
	return;
    }

    if (refresh_only) {
	/* If there's a bar line or green line here, nothing to do */
	if (get_col_overlay(pos_x, NULL)) return;

	/* If there's any result for this column in the cache, it should be
	 * displaying something, but it might be for the wrong fftfreq/window.
	 * We have no way of knowing what it is displaying so force its repaint
	 * with the current parameters.
	 */
	if ((r = recall_result(t, ANY_FFTFREQ, ANY_WINDOW)) != NULL) {
	    /* There's data for this column. */
	    if (r->fft_freq == fft_freq && r->window == window_function) {
		/* Bingo! It's the right result */
		paint_column(pos_x, from_y, to_y, r);
	    } else {
		/* Bummer! It's for something else. Repaint it. */
		repaint_column(pos_x, from_y, to_y, FALSE);
	    }
	} else {
	    /* There are no results in-cache for this column,
	     * so it can't be displaying any spectral data */
	}
    } else {
	color_t ov;
	if (get_col_overlay(pos_x, &ov)) {
	    gui_paint_column(pos_x, from_y, to_y, ov);
	} else
	/* If we have the right spectral data for this column, repaint it */
	if ((r = recall_result(t, fft_freq, window_function)) != NULL) {
	    paint_column(pos_x, from_y, to_y, r);
	} else {
	    /* ...otherwise paint it with the background color */
	    if (pos_x >= min_x && pos_x <= max_x)
		gui_paint_column(pos_x, from_y, to_y, background);

	    /* ...and if it was for a valid time, schedule its calculation */
	    if (DELTA_GE(t, 0.0) && DELTA_LE(t, audio_file_length())) {
		calc_column(pos_x);
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
paint_column(int pos_x, int from_y, int to_y, calc_t *result)
{
    float *logmag;
    float col_logmax;	/* maximum log magnitude in the column */
    int y;
    color_t ov;		/* Overlay color */
    int speclen;
    /* Stuff to detect and report once the presence of out-of-range colors */
    unsigned n_bad_pixels = 0;
    float a_bad_value = 0.0;	/* Init value unused; avoids compiler warning */

    /* Only paint on-screen columns. Off-screen columns Can happen
     * when results for lookahead calculations arrive.
     */
    if (pos_x < min_x || pos_x > max_x) return;

    /* Apply column overlay */
    if (get_col_overlay(pos_x, &ov)) {
	gui_paint_column(pos_x, from_y, to_y, ov);
	return;
    }

    speclen = fft_freq_to_speclen(fft_freq, current_sample_rate());

    logmag = Calloc(maglen, sizeof(*logmag));
    col_logmax = interpolate(logmag, result->spec, from_y, to_y,
    			     current_sample_rate(), speclen);

    /* Auto-adjust brightness if some pixel is brighter than current maximum */
    if (col_logmax > logmax) logmax = col_logmax;

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contrast control.
     */
    gui_lock();		/* Allow pixel-writing access */
    for (y=from_y; y <= to_y; y++) {
        int k = y - min_y;
	float value = (float)20.0 * (logmag[k] - logmax);
	color_t color = colormap(value);
	if (color == no_color) {
	    /* Something went wrong, usually because we're trying to display
	     * an array of nans. The bad_pixels variable avoind blurting 480
	     * error messages. */
	    if (n_bad_pixels++ == 0) a_bad_value = value;
	}
	/* Apply row overlay, if any, otherwise paint the pixel
	 * but don't overlay the green line */
	gui_putpixel(pos_x, y,
		     ((green_line_off || pos_x != disp_offset) && get_row_overlay(y, &ov))
		     ? ov :
		     /* OR in the green line if it's on */
		     ((!green_line_off && pos_x == disp_offset) ? green : 0)
		     | color);
    }
    gui_unlock();

    /* Blurt one error message per column, not 480 */
    if (n_bad_pixels > 0) {
	fprintf(stderr, "%d bad color values in column %d (e.g. %g)\n",
		n_bad_pixels, pos_x, (double)a_bad_value);
    }

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
 * Schedule the FFT thread(s) to calculate the result for a display columns
 */
static void
calc_column(int col)
{
    calc_t *calc = Malloc(sizeof(calc_t));

    calc->fft_freq   = fft_freq;
    calc->window     = window_function;
    calc->t	     = screen_column_to_start_time(col);

    schedule(calc);
}
