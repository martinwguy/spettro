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

/* System header files */

#include <unistd.h>	/* for sleep() */
#include <string.h>	/* for memset() */
#include <errno.h>
#include <ctype.h>	/* for tolower() */
#include <libgen.h>	/* for basename() */

/*
 * Local header files
 */

#include "args.h"
#include "audio.h"
#include "audio_file.h"
#include "audio_cache.h"
#include "axes.h"
#include "barlines.h"
#include "cache.h"
#include "calc.h"
#include "colormap.h"
#include "convert.h"
#include "dump.h"
#include "interpolate.h"
#include "gui.h"
#include "mouse.h"
#include "overlay.h"
#include "paint.h"
#include "scheduler.h"
#include "timer.h"
#include "window.h"	/* for free_windows() */
#include "ui.h"
#include "ui_funcs.h"
#include "key.h"

/*
 * Local functions
 */
static void bad_key(const char *key_name);

/*
 * Local data
 */
static int exit_status = 0;

static audio_file_t *audio_file;       /* The one that's playing at the crosshairs */

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
    if (show_axes) {
	min_x += frequency_axis_width;
	max_x -= note_name_axis_width;
	min_y += bottom_margin;
	max_y -= top_margin;
    }
    maglen = (max_y - min_y) + 1;
    make_row_overlay();

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

    audio_file = af;	/* For now, until we implement playing multiple files */

    /* Initialise the graphics subsystem. */
    /* Note: SDL2 in fullcreen mode may change disp_height and disp_width */
    gui_init(filename);

    init_audio(af, filename);

    /* Apply the -p flag */
    if (disp_time != 0.0) set_playing_time(disp_time);

    start_scheduler(max_threads);

    if (show_axes) draw_axes();

    repaint_display(FALSE); /* Schedules the initial screen refresh */

    start_timer();
    gui_main();

    if (output_file) {
	while (there_is_work()) {abort(); sleep(1); }
	while (jobs_in_flight > 0) {abort(); usleep(100000); }
	gui_output_png_file(output_file);
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

    return exit_status;
}

/* Utility function */
static void
set_window_function(window_function_t new_fn)
{
    if (new_fn != window_function) {
	window_function = new_fn;
	drop_all_work();
	repaint_display(FALSE);
	fprintf(stderr, "Using a %s window\n", window_name(new_fn));
    }
}

/*
 * Process a keystroke.  Also inspects the variables Ctrl and Shift.
 */
void
do_key(enum key key)
{
    switch (key) {

    case KEY_NONE:	/* They pressed something else */
	break;

    case KEY_C:
	if (Shift && !Ctrl) { bad_key("Shift-C"); break; }
	if (Shift && Ctrl) { bad_key("Ctrl-Shift-C"); break; }
	if (!Ctrl && !Shift) {
	    change_colormap();
	    repaint_display(TRUE);
	    break;
	}
	/* Ctrl-C is an alias for Quit */
	exit_status = 1;
	goto quit;
    case KEY_ESC:
	if (Shift || Ctrl) {
	    /* In practice, Ctrl-Esc is taken by my window manager, xfce */
	    bad_key("Ctrl/Shift-Esc");
	    break;
	}
	goto quit;
    case KEY_Q:
    	/* Ctrl-Q also, Shift-Q also, Ctrl-Shift-Q also */
quit:
	gui_quit_main_loop();
	break;

    case KEY_SPACE:	/* Play/Pause/Rewind */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Space"); break; }
#if ECORE_MAIN
    case KEY_PLAY:	/* Extended keyboard's >/|| button (EMOTION only) */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Play"); break; }
#endif

	switch (playing) {
	case PLAYING:
	    pause_audio();
	    break;

	case STOPPED:
restart:
	    set_playing_time(0.0);
	    start_playing();
	    break;

	case PAUSED:
	    if (DELTA_GE(get_playing_time(), audio_files_length())) {
		/* They went "End" while it was paused. Restart from 0 */
		goto restart;
	    }
	    continue_playing();
	    break;
	}
	break;
#if ECORE_MAIN
    case KEY_STOP:	/* Extended keyboard's [] button (EMOTION only) */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Stop"); break; }

	if (playing == PLAYING) pause_audio();
	break;
#endif

    /*
     * Arrow <-/->: Jump back/forward a tenth of a screenful
     * With Shift, a whole screenful. With Ctrl one pixel.
     * With Ctrl-Shift, one second.
     */
    case KEY_LEFT:
    case KEY_RIGHT:
	{
	    double by;
	    if (!Shift && !Ctrl) by = disp_width * step / 10;
	    if (Shift && !Ctrl) by = disp_width * step;
	    if (!Shift && Ctrl) by = step;
	    if (Shift && Ctrl) by = 1.0;
	    time_pan_by(key == KEY_LEFT ? -by : +by);
	}
	break;

    /*
     * Home and End: Go to start or end of piece
     */
    case KEY_HOME:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Home"); break; }
#if ECORE_MAIN
    case KEY_PREV:	/* Extended keyboard's |<< button (EMOTION only) */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl |<<"); break; }
#endif
	set_playing_time(0.0);
	break;
    case KEY_END:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-End"); break; }
#if ECORE_MAIN
    case KEY_NEXT:	/* Extended keyboard's >>| button (EMOTION only) */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl >>|"); break; }
#endif
	set_playing_time(audio_files_length());
	break;

    /*
     * Arrow Up/Down: Pan the frequency axis by a tenth of the screen height.
     * With Shift: by a screenful. With Ctrl, by a pixel.
     * The argument to freq_pan_by() multiplies min_freq and max_freq.
     * Page Up/Down: Pan the frequency axis by a screenful
     */
    case KEY_PGUP:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Page Up"); break; }
    case KEY_PGDN:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Page Down"); break; }
    case KEY_UP:
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-Up Arrow"); break; }
    case KEY_DOWN:
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-Down Arrow"); break; }

	if (key == KEY_UP)
	    freq_pan_by(Ctrl ? v_pixel_freq_ratio():
		        Shift ? max_freq / min_freq :
			pow(max_freq / min_freq, 1/10.0));
	else if (key == KEY_DOWN)
	    freq_pan_by(Ctrl ? 1.0 / v_pixel_freq_ratio() :
		        Shift ? min_freq / max_freq :
			pow(min_freq / max_freq, 1/10.0));
	else if (key == KEY_PGUP)
	    freq_pan_by(max_freq/min_freq);
	else if (key == KEY_PGDN)
	    freq_pan_by(min_freq/max_freq);

	if (show_axes) draw_frequency_axes();
	gui_update_display();
	break;

    /* Zoom on the time axis by a factor of two so that, when zooming in,
     * half of the results are still valid
     */
    case KEY_X:
	if (Ctrl) { bad_key("Ctrl-X"); break; }
	time_zoom_by(Shift ? 2.0 : 0.5);
	repaint_display(FALSE);
	break;

    /* Y/y: Zoom in/out on the frequency axis by a factor of two
     * or by one pixel top and bottom if Ctrl is held.
     */
    case KEY_Y:
	{
	    double by = 2.0;	/* Normally we double/halve */
	    if (Ctrl) {
#if 0
	    	by = v_pixel_freq_ratio();
		by = by * by;	/* the new range includes 2 more pixels */
#else
		/* I can't figure out how to make freq_zoom_by() get it right
		 * so do the one-pixel zoom in a way that works.
		 * This reproduces what freq_zoom_by() does.
		 */
		double vpfr = v_pixel_freq_ratio();
		if (Shift) {	/* Zoom in */
		    max_freq /= vpfr;
		    min_freq *= vpfr;
		} else {	/* Zoom out */
		    max_freq *= vpfr;
		    min_freq /= vpfr;
		}
		/* Limit to fft_freq..Nyquist */
		if (max_freq > current_sample_rate() / 2)
		    max_freq = current_sample_rate() / 2;
		if (min_freq < fft_freq)
		    min_freq = fft_freq;

		if (show_axes) draw_frequency_axes();
		repaint_display(TRUE);
		break;
#endif
	    }
	    freq_zoom_by(Shift ? by : 1.0/by);
	    repaint_display(TRUE);
	}
	break;

    /* Normal zoom-in zoom-out, i.e. both axes. */
    case KEY_PLUS:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Plus"); break; }
	freq_zoom_by(2.0);
	time_zoom_by(2.0);
	repaint_display(FALSE);
	break;
    case KEY_MINUS:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-Minus"); break; }
	freq_zoom_by(0.5);
	time_zoom_by(0.5);
	repaint_display(FALSE);
	break;

    /* B and D choose the Barlett and Doplh windows.
     * b and d change the dynamic range of color spectrum by 6dB.
     * 'b' brightens the dark areas, which is achieved by increasing
     * the dynamic range;
     * 'd' darkens them to reduce visibility of background noise.
     * With Ctrl held, b and d brighten/darken by 1dB.
     */
    case KEY_B:
	if (Shift && Ctrl) { bad_key("Ctrl-Shift-B"); break; }

    	if (Shift && !Ctrl) set_window_function(BARTLETT);
	else if (!Shift && !Ctrl) {
	    change_dyn_range(6.0);
	    repaint_display(TRUE);
	} else if (!Shift && Ctrl) {
	    change_dyn_range(1.0);
	    repaint_display(TRUE);
	}
	break;
    case KEY_D:
	if (Shift && Ctrl) { bad_key("Ctrl-Shift-D"); break; }

	if (Shift && !Ctrl) set_window_function(DOLPH);
	else if (!Shift && !Ctrl) {
	    change_dyn_range(-6.0);
	    repaint_display(TRUE);
	} else if (!Shift && Ctrl) {
	    change_dyn_range(-1.0);
	    repaint_display(TRUE);
	}
	break;

    case KEY_A:				/* Toggle frequency axis */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-A"); break; }

	if (show_axes) {
	    /* Remove frequency axis */
	    min_x = 0; max_x = disp_width - 1;
	    min_y = 0; max_y = disp_height - 1;
	    maglen = (max_y - min_y) + 1;
	} else {
	    /* Add frequency axis */
	    min_x = frequency_axis_width;
	    max_x = disp_width - 1 - note_name_axis_width;
	    /* Add time axis */
	    min_y = bottom_margin;
	    max_y = disp_height - 1 - top_margin;
	    maglen = (max_y - min_y) + 1;
	    draw_axes();
	}
	show_axes = !show_axes;
	/* Adding/removing the top and bottom axes scales the graph vertically
	 * so repaint all */
	repaint_columns(min_x, max_x, min_y, max_y, FALSE);
	break;

    case KEY_W:
	if (Ctrl) { bad_key("Ctrl-W"); break; }

	/* w: Cycle through window functions;
	 * W: cycle backwards */
	if (!Shift) next_window_function();
	if (Shift) prev_window_function();
	printf("Using a %s window\n", window_name(window_function));
	repaint_display(TRUE);
	break;

    /* Toggle staff/piano line overlays */
    case KEY_K:
	if (!Shift && Ctrl) { bad_key("Ctrl-K"); break; }
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-K"); break; }
	if (Shift && !Ctrl) { set_window_function(KAISER); break; }

	/* else regular 'k' for the keyboard overlay */
	/* Drop through */
    case KEY_S:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-S"); break; }
    case KEY_G:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-G"); break; }
	if (key == KEY_K)
	    piano_lines = !piano_lines;
	if (key == KEY_S) {
	    staff_lines = !staff_lines;
	    if (staff_lines) guitar_lines = FALSE;
	}
	if (key == KEY_G) {
	    guitar_lines = !guitar_lines;
	    if (guitar_lines) staff_lines = FALSE;
	}
	make_row_overlay();
	repaint_display(FALSE);
	break;

    case KEY_O:
	/* o: Make a screen dump */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-O"); break; }
	dump_screenshot(audio_file->filename);
	break;

    case KEY_P:
	/* P: Print current UI parameters and derived values */
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-P"); break; }
	{
	    double left_bar_time = get_left_bar_time();
	    double right_bar_time = get_right_bar_time();

	    printf(
"min_freq=%g max_freq=%g dyn_range=%g fft_freq=%g window=%s\n",
 min_freq,   max_freq,   -min_db,     fft_freq,   window_name(window_function));
	    printf(
"%s %g disp_time=%g step=%g from=%g to=%g audio_length=%g\n",
		playing == PLAYING ? "Playing" :
		playing == STOPPED ? "Stopped at" :
		playing == PAUSED  ? "Paused at" : "Doing what? at",
		get_playing_time(), disp_time, step,
		disp_time - disp_offset * step,
		disp_time + (disp_width - disp_offset) * step,
		audio_files_length());
	    if (left_bar_time != UNDEFINED)
		printf("left bar line=%g", left_bar_time);
	    if (right_bar_time != UNDEFINED) {
		if (left_bar_time != UNDEFINED) printf(" ");
		printf("right bar line=%g", right_bar_time);
	    }
	    if (left_bar_time != UNDEFINED && right_bar_time != UNDEFINED)
		printf(" interval=%g bpm=%g",
		    fabs(right_bar_time - left_bar_time),
		    60.0 / fabs(right_bar_time - left_bar_time));
	    if (left_bar_time != UNDEFINED || right_bar_time != UNDEFINED)
		printf("\n");
	}
	break;

    /* Display the current playing time */
    case KEY_T:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-T"); break; }

	printf("%02d:%02d (%g seconds)\n", (int) disp_time / 60,
					   (int) disp_time % 60,
					   disp_time);
	break;

    case KEY_F:
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-F"); break; }

	if (Ctrl) {
	    /* Flip fullscreen mode */
	    gui_fullscreen();
	    break;
	}
	if (Shift) {
	   /* Increase FFT size; decrease FFT frequency */
	   fft_freq /= 2;
	} else {
	    /* Decrease FFT size: increase FFT frequency */
	    if (fft_freq_to_speclen(fft_freq, current_sample_rate()) > 1)
		fft_freq *= 2;
	}
	drop_all_work();

	/* Any calcs that are currently being performed will deliver
	 * a result for the old speclen, which will be ignored (or cached)
	 */
	repaint_display(FALSE);
	break;

    /* Set left or right bar line position to current play position */
    case KEY_L:
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-L"); }

	if (!Shift && !Ctrl) set_left_bar_time(disp_time);
    	else if (Shift && !Ctrl) set_window_function(BLACKMAN);
	else if (Ctrl && !Shift) repaint_display(FALSE);
	break;
    case KEY_R:
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-R"); break; }

	if (!Shift && !Ctrl) set_right_bar_time(disp_time);
	if (Shift && !Ctrl) set_window_function(RECTANGULAR);
	if (Ctrl && !Shift) {
	    drop_all_work();
	    drop_all_results();
	    repaint_display(FALSE);
	}
	break;

    /* Keys for window function that are not already claimed */
    case KEY_H:
	if (!Shift && !Ctrl) { bad_key("h"); break; }
	if (!Shift && Ctrl) { bad_key("Ctrl-H"); break; }
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-H"); break; }
	if (Shift && !Ctrl) set_window_function(HANN);
	break;
    case KEY_N:
	if (!Shift && !Ctrl) { bad_key("n"); break; }
	if (!Shift && Ctrl) { bad_key("Ctrl-N"); break; }
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-N"); break; }
	if (Shift && !Ctrl) set_window_function(NUTTALL);
	break;
    case KEY_M:
	if (!Shift && !Ctrl) { bad_key("m"); break; }
	if (!Shift && Ctrl) { bad_key("Ctrl-M"); break; }
	if (Shift && Ctrl) { bad_key("Shift-Ctrl-M"); break; }
	if (Shift && !Ctrl) set_window_function(HAMMING);
	break;

    /* softvol volume controls */
    case KEY_9:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl 9"); break; }
	softvol *= 0.9;
fprintf(stderr, "Soltvol = %g\n", softvol);
	break;

    case KEY_0:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl 0"); break; }
	softvol /= 0.9;
fprintf(stderr, "Soltvol = %g\n", softvol);
	break;

    /* Beats per bar */

    /* Under SDL2, Shift-number gives the punctuation characters above them
     * and Shift-Ctrl-number gives both Ctrl+Shift+N and the punctuation char.
     */
    case KEY_1: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-1"); break; }
    case KEY_2: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-2"); break; }
    case KEY_3: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-3"); break; }
    case KEY_4: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-4"); break; }
    case KEY_5:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-5"); break; }
    case KEY_6: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-6"); break; }
    case KEY_7: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-7"); break; }
    case KEY_8:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-8"); break; }

    /* Under SDL2, Ctrl-F1 to F12 seem not to be delivered. Shift-Fn yes. */
    case KEY_F1: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F1"); break; }
    case KEY_F2: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F2"); break; }
    case KEY_F3: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F3"); break; }
    case KEY_F4: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F4"); break; }
    case KEY_F5:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F5"); break; }
    case KEY_F6: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F6"); break; }
    case KEY_F7: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F7"); break; }
    case KEY_F8: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F8"); break; }
    case KEY_F9: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F9"); break; }
    case KEY_F10:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F10"); break; }
    case KEY_F11: 
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F11"); break; }
    case KEY_F12:
	if (Shift || Ctrl) { bad_key("Shift/Ctrl-F12"); break; }

	switch (key) {
	case KEY_1: case KEY_F1: set_beats_per_bar(1); break;
	case KEY_2: case KEY_F2: set_beats_per_bar(2); break;
	case KEY_3: case KEY_F3: set_beats_per_bar(3); break;
	case KEY_4: case KEY_F4: set_beats_per_bar(4); break;
	case KEY_5: case KEY_F5: set_beats_per_bar(5); break;
	case KEY_6: case KEY_F6: set_beats_per_bar(6); break;
	case KEY_7: case KEY_F7: set_beats_per_bar(7); break;
	case KEY_8: case KEY_F8: set_beats_per_bar(8); break;
		    case KEY_F9: set_beats_per_bar(9); break;
		    case KEY_F10: set_beats_per_bar(10); break;
		    case KEY_F11: set_beats_per_bar(11); break;
		    case KEY_F12: set_beats_per_bar(12); break;
	default: break; /* Shut the compiler up about unhandled KEYs */
	}
	break;

    default:
	fprintf(stderr, "Bogus KEY_ number %d\n", key);
    }
}

static void
bad_key(const char *key_name)
{
    fprintf(stderr, "%s doesn't do anything\n", key_name);
}
