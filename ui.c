/* Functions performing UI actions */

#include "spettro.h"
#include "ui.h"
#include "audio.h"
#include "scheduler.h"
#include "timer.h"
#include "main.h"

#include <math.h>

/*
 * Jump forwards or backwards in time, scrolling the display accordingly.
 */
void
time_pan_by(double by)
{
    double playing_time;

    playing_time = disp_time + by;

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

    repaint_display();
}

/* Pan the display on the vertical axis by changing min_freq and max_freq
 * by a factor.
 */
void
freq_pan_by(double by)
{
    min_freq *= by;
    max_freq *= by;
    repaint_display();
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

    repaint_display();
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

    repaint_display();
}
