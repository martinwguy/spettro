/*
 * ui_funcs.c: Functions performing UI actions.
 *
 * It's up to the caller to call repaint_display() to show any changes
 * except for time pans, which are always updated when the timer ticks.
 */

#include "spettro.h"
#include "ui_funcs.h"
#include "audio.h"
#include "axes.h"
#include "convert.h"
#include "scheduler.h"
#include "timer.h"
#include "main.h"
#include "gui.h"

#include <math.h>

/*
 * Jump forwards or backwards in time, scrolling the display accordingly.
 */
void
time_pan_by(double by)
{
    double playing_time;

    playing_time = get_playing_time() + by;

    if (playing_time < 0.0 + DELTA) playing_time = 0.0;

    /* If we're at/after the end of the piece, stop */
    if (playing_time > audio_length - DELTA) playing_time = audio_length;
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
    if (ppsec * by > sample_rate) {
    	fprintf(stderr, "Limiting time zoom to one sample per column\n");
	return;
    }
    ppsec *= by;
    step = 1 / ppsec;

    /* Change the screen-scrolling speed to match */
    change_timer_interval(step);

    /* Zooming by < 1.0 increases the step size */
    if (by < 1.0) reschedule_for_bigger_step();
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
    /* Limit top */
    if (max_freq > sample_rate / 2) {
	min_freq /= max_freq / (sample_rate / 2);
	by /= max_freq / (sample_rate / 2);
	max_freq = sample_rate / 2;
    }
    /* Limit bottom */
    if (min_freq < fft_freq) {
	max_freq *= fft_freq / min_freq;
	by *= fft_freq / min_freq;
	min_freq = fft_freq;
    }

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
		repaint_column(x, max_y - by_pixels + 1, max_y, TRUE);
	    }
	    if (by_pixels < 0) {
		/* Moving to lower frequencies: repaint the bottom rows */
		repaint_column(x, min_y, min_y - by_pixels - 1, TRUE);
	    }
	}
    }
}

/* Zoom the frequency axis by a factor, staying centred on the centre.
 *
 * A zoom-in (by > 1.0) reduces the range of displayed frequency values, while
 * a zoom-out (by < 1.0) increases the range of displayed frequencies
 * In each case, remaining centred on the middle of the graphic.
 */
void
freq_zoom_by(double by)
{
    /* We want to stay centred on the frequency at the middle of the screen
     * so convert max/min to centre/range */
    double center_frequency = sqrt(min_freq * max_freq);
    double range = max_freq / min_freq;

    /* If by == 2.0, new_range = sqrt(range)
     * if by == 0.5, new_range = range squared
     * general case: new_range = range ** (1/by)
     */
    range = pow(range, 1.0 / by);

    /* Convert center/range back to min/max */
    max_freq = center_frequency * sqrt(range);
    min_freq = center_frequency / sqrt(range);

    /* Limit to fft_freq..Nyquist */
    if (max_freq > sample_rate / 2) max_freq = sample_rate / 2;
    if (min_freq < fft_freq) min_freq = fft_freq;

    if (show_axes) draw_frequency_axis();
}

/* Change the color scale's dynamic range, thereby changing the brightness
 * of the darker areas.
 */
void
change_dyn_range(double by)
{
    /* As min_db is negative, subtracting from it makes it bigger */
    min_db -= by;

    /* min_db should not go positive */
    if (min_db > -6.0) min_db = -6.0;
}
