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
 * If they release Control before MouseUp, no change should be made.
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
#if USE_LIBAV
#include "libavformat/version.h"
#endif

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
 * Local data
 */
static int exit_status = 0;

int
main(int argc, char **argv)
{
    char *filename;

    process_args(&argc, &argv);

    /* Set default values for unset parameters */
    filename = (argc > 0) ? argv[0] : "audio.wav";

    /* Open the audio file to find out sampling rate, length and to be able
     * to fetch pixel data to be converted into spectra.
     * Emotion seems not to let us get the raw sample data or sampling rate
     * and doesn't know the file length until the "open_done" event arrives
     * so we use libsndfile, libaudiofile or libsox for that.
     */
    if ((audio_file = open_audio_file(filename)) == NULL) {
    	gui_quit();
	exit(1);
    }

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
    speclen = fft_freq_to_speclen(fft_freq);
    maglen = (max_y - min_y) + 1;

    make_row_overlay();

    /* Initialise the graphics subsystem. */
    /* Note: SDL2 in fullcreen mode may change disp_height and disp_width */
    gui_init(filename);

    init_audio(audio_file, filename);

    /* Apply the -p flag */
    if (disp_time != 0.0) set_playing_time(disp_time);

    start_scheduler(max_threads);

    if (show_axes) draw_axes();

    repaint_display(FALSE); /* Schedules the initial screen refresh */

    gui_update_display();

    start_timer();
    gui_main();

    if (output_file) {
	while (there_is_work()) {abort(); sleep(1);}
	while (jobs_in_flight > 0) {abort(); usleep(100000);}
	gui_output_png_file(output_file);
    }

    gui_quit();

    /* Free memory to make valgrind happier */
    drop_all_work();
    drop_all_results();
    no_audio_cache();
    free_interpolate_cache();
    free_row_overlay();
    free_windows();
    close_audio_file(audio_file);

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
    }
}

/*
 * Process a keystroke.  Also inspects the variables Control and Shift.
 */
void
do_key(enum key key)
{
    switch (key) {

    case KEY_NONE:	/* They pressed something else */
	break;

    case KEY_C:
	if (!Control && !Shift) {
	    change_colormap();
	    repaint_display(TRUE);
	    break;
	}
	/* Only Control-C is an alias for Quit */
	if (!Control) break;	
	exit_status = 1;
    case KEY_ESC:
    case KEY_Q:
	gui_quit_main_loop();
	break;

    case KEY_SPACE:	/* Play/Pause/Rewind */
#if ECORE_MAIN
    case KEY_PLAY:	/* Extended keyboard's >/|| button (EMOTION only) */
#endif
	switch (playing) {
	case PLAYING:
	    pause_audio();
	    break;

	case STOPPED:
	    set_playing_time(0.0);
	    start_playing();
	    break;

	case PAUSED:
	    continue_playing();
	    break;
	}
	break;
#if ECORE_MAIN
    case KEY_STOP:	/* Extended keyboard's [] button (EMOTION only) */
	if (playing == PLAYING) pause_audio();
	break;
#endif

    /*
     * Arrow <-/->: Jump back/forward a tenth of a screenful
     * With Shift, a whole screenful. With Control one pixel.
     * With Control-Shift, one second.
     */
    case KEY_LEFT:
    case KEY_RIGHT:
	{
	    double by;
	    if (!Shift && !Control) by = disp_width * step / 10;
	    if (Shift && !Control) by = disp_width * step;
	    if (!Shift && Control) by = step;
	    if (Shift && Control) by = 1.0;
	    time_pan_by(key == KEY_LEFT ? -by : +by);
	}
	break;

    /*
     * Home and End: Go to start or end of piece
     */
    case KEY_HOME:
#if ECORE_MAIN
    case KEY_PREV:	/* Extended keyboard's |<< button (EMOTION only) */
#endif
	set_playing_time(0.0);
	break;
    case KEY_END:
#if ECORE_MAIN
    case KEY_NEXT:	/* Extended keyboard's >>| button (EMOTION only) */
#endif
	set_playing_time(audio_file_length(audio_file));
	break;

    /*
     * Arrow Up/Down: Pan the frequency axis by a tenth of the screen height.
     * With Shift: by a screenful. With Control, by a pixel.
     * The argument to freq_pan_by() multiplies min_freq and max_freq.
     * Page Up/Down: Pan the frequency axis by a screenful
     */
    case KEY_PGUP:
    case KEY_PGDN:
	if (Shift || Control) break;
    case KEY_UP:
    case KEY_DOWN:
	if (Shift && Control) break;

	if (key == KEY_UP)
	    freq_pan_by(Control ? v_pixel_freq_ratio():
		        Shift ? max_freq / min_freq :
			pow(max_freq / min_freq, 1/10.0));
	else if (key == KEY_DOWN)
	    freq_pan_by(Control ? 1.0 / v_pixel_freq_ratio() :
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
	if (Control) break;
	time_zoom_by(Shift ? 2.0 : 0.5);
	repaint_display(FALSE);
	break;

    /* Y/y: Zoom in/out on the frequency axis */
    case KEY_Y:
	{
	    double by = 2.0;	/* Normally we double/halve */
	    if (Control) {
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
		if (max_freq > audio_file->sample_rate / 2)
		    max_freq = audio_file->sample_rate / 2;
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
	freq_zoom_by(2.0);
	time_zoom_by(2.0);
	repaint_display(FALSE);
	break;
    case KEY_MINUS:
	freq_zoom_by(0.5);
	time_zoom_by(0.5);
	repaint_display(FALSE);
	break;

    /* Change dynamic range of color spectrum, like a brightness control.
     * Star should brighten the dark areas, which is achieved by increasing
     * the dynrange;
     * Slash instead darkens them to reduce visibility of background noise.
     */
    case KEY_B:
    	if (Shift && !Control) { set_window_function(BARTLETT); break; }
    case KEY_STAR:
	if (Shift || Control) break;
	change_dyn_range(6.0);
	repaint_display(TRUE);
	break;
    case KEY_D:
	if (Shift && !Control) { set_window_function(DOLPH); break; }
    case KEY_SLASH:
	if (Shift || Control) break;
	change_dyn_range(-6.0);
	repaint_display(TRUE);
	break;

    case KEY_A:				/* Toggle frequency axis */
	if (Shift || Control) break;
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
	/* Cycle through window functions */
	next_window_function();
	printf("Using %s window\n", window_name(window_function));
	repaint_display(TRUE);
	break;

    /* Toggle staff/piano line overlays */
    case KEY_K:
	if (Shift && !Control) { set_window_function(KAISER); break; }
	/* else drop through */
    case KEY_S:
    case KEY_G:
	if (Shift || Control) break;
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
	if (!Control && !Shift) dump_screenshot();
	break;

    case KEY_P:
	/* P: Print current UI parameters and derived values */
	if (!Control && !Shift) {
	    double left_bar_time = get_left_bar_time();
	    double right_bar_time = get_right_bar_time();

	    printf("Spectrogram of %s\n", audio_file->filename);
	    printf(
"min_freq=%g max_freq=%g fft_freq=%g dyn_range=%g speclen=%d\n",
 min_freq,   max_freq,   fft_freq,   -min_db,     speclen);
	    printf(
"playing %g disp_time=%g step=%g from=%g to=%g audio_length=%g\n",
		get_playing_time(), disp_time, step,
		disp_time - disp_offset * step,
		disp_time + (disp_width - disp_offset) * step,
		audio_file_length(audio_file));
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
	if (Shift || Control) break;
	printf("%02d:%02d (%g seconds)\n", (int) disp_time / 60,
					   (int) disp_time % 60,
					   disp_time);
	break;

    case KEY_F:
	if (Control) {
	    /* Flip fullscreen mode */
	    gui_fullscreen();
	    break;
	}
	if (Shift) {
	   /* Increase FFT size; decrease FFT frequency */
	   fft_freq /= 2;
	} else {
	   /* Decrease FFT size: increase FFT frequency */
	   if (speclen > 1)
	   fft_freq *= 2;
	}
	speclen = fft_freq_to_speclen(fft_freq);
	drop_all_work();

	/* Any calcs that are currently being performed will deliver
	 * a result for the old speclen, which will be ignored (or cached)
	 */
	repaint_display(FALSE);
	break;

    /* Set left or right bar line position to current play position */
    case KEY_L:
	if (!Shift && !Control) set_left_bar_time(disp_time);
    	if (Shift && !Control) set_window_function(BLACKMAN);
	if (Control && !Shift) repaint_display(FALSE);
	break;
    case KEY_R:
	if (!Shift && !Control) set_right_bar_time(disp_time);
	if (Shift && !Control) set_window_function(RECTANGULAR);
	if (Control && !Shift) {
	    drop_all_work();
	    drop_all_results();
	    repaint_display(FALSE);
	}
	break;

    /* Keys for window function that are not already claimed */
    case KEY_H:
	if (Shift && !Control) set_window_function(HANN);
	break;
    case KEY_N:
	if (Shift && !Control) set_window_function(NUTTALL);
	break;
    case KEY_M:
	if (Shift && !Control) set_window_function(HAMMING);
	break;

    /* softvol volume controls */
    case KEY_9:
	softvol *= 0.9;
fprintf(stderr, "Soltvol = %g\n", softvol);
	break;

    case KEY_0:
	softvol /= 0.9;
fprintf(stderr, "Soltvol = %g\n", softvol);
	break;

    /* Beats per bar */
    case KEY_1: set_beats_per_bar(1); break;
    case KEY_2: set_beats_per_bar(2); break;
    case KEY_3: set_beats_per_bar(3); break;
    case KEY_4: set_beats_per_bar(4); break;
    case KEY_5: set_beats_per_bar(5); break;
    case KEY_6: set_beats_per_bar(6); break;
    case KEY_7: set_beats_per_bar(7); break;
    case KEY_8: set_beats_per_bar(8); break;
    case KEY_F1: set_beats_per_bar(1); break;
    case KEY_F2: set_beats_per_bar(2); break;
    case KEY_F3: set_beats_per_bar(3); break;
    case KEY_F4: set_beats_per_bar(4); break;
    case KEY_F5: set_beats_per_bar(5); break;
    case KEY_F6: set_beats_per_bar(6); break;
    case KEY_F7: set_beats_per_bar(7); break;
    case KEY_F8: set_beats_per_bar(8); break;
    case KEY_F9: set_beats_per_bar(9); break;
    case KEY_F10: set_beats_per_bar(10); break;
    case KEY_F11: set_beats_per_bar(11); break;
    case KEY_F12: set_beats_per_bar(12); break;

    default:
	fprintf(stderr, "Bogus KEY_ number %d\n", key);
    }
}
