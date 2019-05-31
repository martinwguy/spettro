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
 * Program: spettro
 *	Play an audio file displaying a scrolling log-frequency spectrogram.
 *
 * File: main.c
 *	Main routine, parameter handling, reading and servicing key strokes,
 *	scheduling FFT calculations and receiving the results,
 *	the FFT result cache and drawing overlays on rows and columns.
 *
 * The audio file is given as a command-line argument
 * A window opens showing a graphical representation of the audio file:
 * each frame of audio samples is shown as a vertical bar whose colors are
 * taken from the "heat maps" of "sox spectrogram".
 *
 * The color at each point represents the energy in the sound at some
 * frequency (band) at a certain moment in time (over a short period).
 * The vertical axis, representing frequency, is logarithmic, giving an
 * equal number of pixel rows in each octave of the scale, by default
 * 9 octaves from 27.5 Hz (very bottom A) to 14080 Hz (the toppest A
 * we can hear.)
 *
 * At startup, the start of the piece is at the centre of the window and
 * the first seconds of the audio file are shown on the right half. The
 * left half is all grey.
 *
 * If you hit play (press 'space'), the audio starts playing and the
 * display scrolls left so that the current playing position remains at the
 * centre of the window. Another space pauses the playback, another makes it
 * continue from where it was. When it reaches the end of piece, the playback
 * stops; pressing space makes it start again from the beginning.
 *
 * If you resize the window the displayed image is zoomed.
 *
 * For command-line options and key bindings, see Usage.
 *
 * it runs in three types of thread:
 * - the main thread handles GUI events, starts/stops the audio player,
 *   tells the calc thread what to calculate, receives results, and
 *   displays them.
 * - The calc thread performs FFTs and reports back when they're done.
 * - the timer thread is called periodically to scroll the display in sync
 *   with the audio playback. The actual scrolling is done in the main loop
 *   in response to an event posted by the timer thread.
 *
 * == Mouse handling ==
 *
 * On Ctrl-mouse down, the left and right bar lines are set at the
 * mouse position. *DONE*
 *
 * Mouse click and drag should pan the display in real time.
 *
 * The bar line should appear when you press the button
 * and if you move it while holding the button, the bar line should move too,
 * being positioned definitively when you release the mouse button.
 * THe other bar lines on each side should expand and collapse to match
 * If they release Ctrl before MouseUp, no change should be made.
 *
 *	Martin Guy <martinwguy@gmail.com>, Dec 2016 - May 2017.
 */

#include "spettro.h"

/*
 * System header files
 */
#include <unistd.h>	/* for sleep() */

/*
 * Local header files
 */
#include "args.h"
#include "audio.h"
#include "audio_cache.h"
#include "audio_file.h"
#include "axes.h"
#include "cache.h"
#include "gui.h"
#include "interpolate.h"
#include "overlay.h"
#include "paint.h"
#include "scheduler.h"
#include "timer.h"
#include "window.h"	/* for free_windows() */
#include "ui.h"

int
main(int argc, char **argv)
{
    char *filename;
    audio_file_t *af;

    process_args(&argc, &argv);

    /* Set default values for unset parameters */
    filename = (argc > 0) ? argv[0] : "audio.wav";

    /* Set variables with derived values */
    disp_offset = disp_width / 2;
    step = 1 / ppsec;
    min_x = 0; max_x = disp_width - 1;
    min_y = 0; max_y = disp_height - 1;
    if (show_freq_axes) {
	min_x += frequency_axis_width;
	max_x -= note_name_axis_width;
    }
    if (show_time_axes) {
	min_y += bottom_margin;
	max_y -= top_margin;
    }
    maglen = (max_y - min_y) + 1;

    /* Open the audio file to find out sampling rate, length and to be able
     * to fetch pixel data to be converted into spectra.
     * Emotion seems not to let us get the raw sample data or sampling rate
     * and doesn't know the file length until the "open_done" event arrives
     * so we use libsndfile, libaudiofile or libsox for that.
     */
    if ((af = open_audio_file(filename)) == NULL) {
    	gui_quit();
	exit(1);
    }

    /* Initialise the graphics subsystem. */
    /* Note: SDL2 in fullcreen mode may change disp_height and disp_width */
    gui_init(filename);

    /* Must happen after colors (green,white) are defined */
    make_row_overlay();	

    init_audio(af, filename);

    /* Apply the -p flag */
    if (disp_time != 0.0) set_playing_time(disp_time);

    start_scheduler(max_threads);

    draw_axes();

    repaint_display(FALSE); /* Schedules the initial screen refresh */

    start_timer();
    gui_main();

    if (output_file) {
	while (there_is_work()) {abort(); sleep(1); }
	while (jobs_in_flight > 0) {abort(); usleep(100000); }
	green_line_off = TRUE;
	repaint_column(disp_offset, min_y, max_y, FALSE);
	gui_update_column(disp_offset);
	gui_output_png_file(output_file);
	green_line_off = FALSE;
    }

    gui_quit();

    /* Free memory to make valgrind happier */
    drop_all_work();
    drop_all_results();
    no_audio_cache(af);
    free_interpolate_cache();
    free_row_overlay();
    free_windows();
    close_audio_file(af);

    return 0;
}
