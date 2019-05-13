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
 * ui_funcs.c: Functions performing UI actions.
 *
 * It's up to the caller to call repaint_display() to show any changes
 * except for time pans, which are always updated when the timer ticks and
 * frequency pans, which know more about what to redwar than the caller.
 */

#include "spettro.h"
#include "ui_funcs.h"

#include "audio.h"
#include "axes.h"
#include "convert.h"
#include "gui.h"
#include "paint.h"
#include "scheduler.h"
#include "timer.h"
#include "ui.h"

#include <values.h>	/* for DBL_MAX */

/*
 * Jump forwards or backwards in time, scrolling the display accordingly.
 */
void
time_pan_by(double by)
{
    double playing_time;
    double audio_length = audio_files_length();

    playing_time = get_playing_time() + by;

    if (DELTA_LE(playing_time, 0.0)) playing_time = 0.0;

    /* If we're at/after the end of the piece, stop */
    if (DELTA_GE(playing_time, audio_length))
	playing_time = audio_length;

    if (playing_time == audio_length) {
	/* If playing, stop */
	stop_playing();
	playing_time = audio_length;
    }

    set_playing_time(playing_time);

    /* If moving left after it has come to the end and stopped,
     * we want to go into pause state */
    if (by < 0.0 && playing == STOPPED && playing_time <= audio_length) {
       playing = PAUSED;
    }

    /* The screen will be scrolled at the next timer event */
}

/* Zoom the time axis on disp_time.
 * Only ever done by 2.0 or 0.5 to improve result cache usefulness.
 * The recalculation of every other pixel column is triggered by
 * the call to repaint_display().
 *
 * We limit the zoom to one sample difference per pixel.
 */
void
time_zoom_by(double by)
{
    if (ppsec * by > current_sample_rate()) {
    	fprintf(stderr, "Limiting time zoom to one sample per column\n");
	return;
    }
    ppsec *= by;
    step = 1 / ppsec;

    /* Change the screen-scrolling speed to match */
    change_timer_interval(step);

    /* Zooming by < 1.0 increases the step size */
    if (by < 1.0) reschedule_for_bigger_step();

    if (show_axes) {
	draw_time_axis();
	draw_status_line();
    }
}

/* Pan the display on the vertical axis by changing min_freq and max_freq
 * by a factor.
 * if "by" > 1.0, that moves up the frequency axis (moving the graphic down)
 * if "by" < 1.0, that moves down the frequency axis (moving the graphic up)
 */
void
freq_pan_by(double by)
{
    int by_pixels;	/* How many pixels to scroll by */

    min_freq *= by;
    max_freq *= by;

    /* How many pixels represent a frequency ratio of "by"?
     * one_pixel ^ by_pixels == by
     * exp(log(one_pixel) * by_pixels) == by
     * log(one_pixel) * by_pixels == log(by)
     * by_pixels == log(by) / log(one_pixel);
     */
    by_pixels = lrint(log(by) / log(v_pixel_freq_ratio()));

    /* If the scroll is more than a screenful, repaint all displayed columns */
    if (abs(by_pixels) >= max_y - min_y + 1) {
	repaint_display(TRUE);
    } else {
        register int x;

	gui_v_scroll_by(by_pixels);

	/* repaint the newly-exposed area */
	for (x=min_x; x <= max_x; x++) {
	    if (by_pixels > 0) {
		/* Moving to higher frequencies: repaint the top rows */
		repaint_column(x, max_y - (by_pixels - 1), max_y, TRUE);
	    }
	    if (by_pixels < 0) {
		/* Moving to lower frequencies: repaint the bottom rows */
		repaint_column(x, min_y, min_y + (-by_pixels - 1), TRUE);
	    }
	}
    }
    if (show_axes) {
	draw_frequency_axes();
	draw_status_line();
    }
}

/* Zoom the frequency axis by a factor, staying centred on the centre.
 *
 * A zoom-in (by > 1.0) reduces the range of displayed frequency values, while
 * a zoom-out (by < 1.0) increases the range of displayed frequencies
 * In each case, remaining centred on the middle of the graphic.
 */

#define MAX_RANGE DBL_MAX/2

void
freq_zoom_by(double by)
{
    /* We want to stay centred on the frequency at the middle of the screen
     * so convert max/min to centre/range */
    double center_frequency = sqrt(min_freq * max_freq);
    double range = max_freq / min_freq;
    double old_min_freq = min_freq, old_max_freq = max_freq;

    /* If by == 2.0, new_range = sqrt(range)
     * if by == 0.5, new_range = range squared
     * general case: new_range = range ** (1/by)
     */
    range = pow(range, 1.0 / by);

    /* This stops the frequency axis calculator from going into an infinite
     * loop */
    if (range > MAX_RANGE || !isfinite(range)) {
    	/* Silly zoom-out bursts the frequency axis. Refuse */
	return;
    }

    /* Convert center/range back to min/max */
    max_freq = center_frequency * sqrt(range);
    min_freq = center_frequency / sqrt(range);

    /* Zoom limit: saves the axis calculator from an infinite loop */
    if (log(v_pixel_freq_ratio()) == 0.0) {
	fprintf(stderr, "Zoom limit reached\n");
	min_freq = old_min_freq;
	max_freq = old_max_freq;
	return;
    }

    if (show_axes) {
    	draw_frequency_axes();
    	draw_status_line();
    }
}

/* Change the color scale's dynamic range, thereby changing the brightness
 * of the darker areas.
 */
void
change_dyn_range(double by)
{
    dyn_range += by;

    /* dyn_range should not go zero or negative, so set minimum of 1dB */
    if (DELTA_LT(dyn_range, 1.0)) dyn_range = 1.0;

    if (show_axes) draw_status_line();
}
