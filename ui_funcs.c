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
 */
void
time_zoom_by(double by)
{
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
    double log_one_pixel = log(max_freq/min_freq) / (max_y - min_y);
    /* How many times do we have to multiply one_pixel by to get "by"?
     * one_pixel ^ by_pixels == by
     * exp(log(one_pixel) * by_pixels) == by
     * log(one_pixel) * by_pixels == log(by)
     * by_pixels == log(by) / log(one_pixel);
     */
    int by_pixels;
    register int x;

    min_freq *= by;
    max_freq *= by;
    /* Limit top */
    if (max_freq > sample_rate / 2) {
	min_freq /= max_freq / (sample_rate / 2);
	by /= max_freq / (sample_rate / 2);
	max_freq = sample_rate / 2;
    }
    /* Limit bottom */
    if (min_freq < fftfreq) {
	max_freq *= fftfreq / min_freq;
	by *= fftfreq / min_freq;
	min_freq = fftfreq;
    }

    by_pixels = lrint(log(by) / log_one_pixel);
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

/* Zoom the frequency axis by a factor, staying centred on the centre.
 * Values > 1.0 zoom in; values < 1.0 zoom out.
 */
void
freq_zoom_by(double by)
{
    /* Don't let them turn the graphic upside-down! */
    if (max_freq / by <= min_freq * by + DELTA) return;

    /* Limit frequency range */
    max_freq /= by;
    if (max_freq > sample_rate / 2) max_freq = sample_rate / 2;
    min_freq *= by;
    if (min_freq < fftfreq) min_freq = fftfreq;

    if (yflag) draw_frequency_axis();
}

/* Change the color scale's dyna,ic range, thereby changing the brightness
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
