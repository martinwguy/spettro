/*
 * ui_funcs.c: Functions performing UI actions.
 *
 * It's up to the caller to call repaint_display() to show any changes
 * except for time pans, which are always updated when the timer ticks.
 */

#include "spettro.h"
#include "ui_funcs.h"
#include "audio.h"
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

    if (playing_time < 0.0) playing_time = 0.0;

    /* If we're at/after the end of the piece, stop */
    if (playing_time > audio_length) playing_time = audio_length;
    if (playing_time == audio_length) {
	/* If playing, stop */
	if (playing == PLAYING) {
	    pause_audio();
	    playing = STOPPED;
	}
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
    double log_one_pixel = log(max_freq/min_freq) / (disp_height-1);
    /* How many times do we have to multiply one_pixel by to get "by"?
     * one_pixel ^ by_pixels == by
     * exp(log(one_pixel) * by_pixels) == by
     * log(one_pixel) * by_pixels == log(by)
     * by_pixels == log(by) / log(one_pixel);
     */
    int by_pixels = lrint(log(by) / log_one_pixel);
    register int x;

    gui_v_scroll_by(by_pixels);
    min_freq *= by;
    max_freq *= by;

    /* repaint the newly-exposed area */
    for (x=0; x < disp_width; x++) {
	if (by_pixels > 0) {
	    /* Moving to higher frequencies: repaint the top rows */
	    repaint_column(x, disp_height - by_pixels, disp_height-1, FALSE);
	}
	if (by_pixels < 0) {
	    /* Moving to lower frequencies: repaint the bottom rows */
	    repaint_column(x, 0, -by_pixels - 1, FALSE);
	}
    }
    green_line();
    gui_update_display();
}

/* Zoom the frequency axis by a factor, staying centred on the centre.
 * Values > 1.0 zoom in; values < 1.0 zoom out.
 */
void
freq_zoom_by(double by)
{
    double  centre = sqrt(min_freq * max_freq);
    double   range = max_freq / centre;

    range /= by;
    min_freq = centre / range;
    max_freq = centre * range;
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
