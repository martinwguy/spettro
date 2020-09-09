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
 * For command-line options and key bindings, see Usage in args.c.
 *
 * It runs in three types of thread:
 * - the main thread handles GUI events, starts/stops the audio player,
 *   tells the calc thread what to calculate, receives results, and
 *   displays them.
 * - The calc thread performs FFTs and reports back when they're done.
 * - the timer thread is called periodically to scroll the display in sync
 *   with the audio playback. The actual scrolling is done in the main loop
 *   in response to an event posted by the timer thread.
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
    audio_file_t *af = NULL;
    char *filename;

    process_args(&argc, &argv);

    /* Set variables with derived values */
    disp_offset = disp_width / 2;
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

    /* Process the filename argument */
    if (argc != 1) {
	fprintf(stderr, "You must name one audio file.\n");
	exit(1);
    }
    filename = argv[0];

    if ((af = open_audio_file(filename)) == NULL) {
	fprintf(stderr, "Cannot read ");
	perror(filename);
	exit(1);
    }

    /* If they set disp_time with -t or --start, check that it's
     * within the audio and make it coincide with the start of a column.
     */
    if (start_time > audio_file_length()) {
	fprintf(stderr,
		"Starting time (%g) is beyond the end of the audio (%g).\n",
		disp_time, audio_file_length());
	set_disp_time(audio_file_length());
    } else {
	set_disp_time(start_time);
    }

    /* Initialize the graphics subsystem. */
    /* SDL2 in fullscreen mode may change disp_height and disp_width */
    gui_init(filename);
    /* The row overlay (piano notes/staff lines) doesn't depend on
     * the sample rate, only on min/max_freq, so it doesn't change
     * from file to file */
    make_row_overlay();	

    /* Initialize the audio subsystem. */
    init_audio(af, filename);

    /* Apply the -t flag */
    if (disp_time != 0.0) set_playing_time(disp_time);

    start_scheduler(max_threads);

    draw_axes();

    repaint_display(FALSE); /* Schedules the initial screen refresh */

    start_timer();

    gui_main();

    stop_timer();
    stop_scheduler();
    gui_quit();

    /* Free memory to make valgrind happier */
    drop_all_work();
    drop_all_results();
    free_interpolate_cache();
    free_row_overlay();
    free_windows();
    close_audio_file(af);

    return 0;
}
