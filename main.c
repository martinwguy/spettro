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
 * taken from the "heat maps" of sndfile-spectrogram.
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
 *   with the audio playback. THe actual scrolling is done in the main loop
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

#include <stdlib.h>
#include <unistd.h>	/* for sleep() */
#include <math.h>	/* for lrint() */
#include <malloc.h>
#include <string.h>	/* for memset() */
#include <errno.h>
#include <math.h>
#include <ctype.h>	/* for tolower() */

/*
 * Local header files
 */

#include "audio.h"
#include "audio_file.h"
#include "cache.h"
#include "calc.h"
#include "colormap.h"
#include "interpolate.h"
#include "gui.h"
#include "mouse.h"
#include "overlay.h"
#include "scheduler.h"
#include "speclen.h"
#include "timer.h"
#include "ui_funcs.h"
#include "main.h"
#include "key.h"

/*
 * Function prototypes
 */

/* Helper functions */
static void	calc_columns(int from, int to);

/*
 * State variables
 */

/* GUI state variables */
       int disp_width	= 640;	/* Size of displayed drawing area in pixels */
       int disp_height	= 480;
       double disp_time	= 0.0;	/* When in the audio file is the crosshair? */
       int disp_offset; 	/* Crosshair is in which display column? */
       double min_freq	= 27.5;		/* Range of frequencies to display: */
       double max_freq	= 14080;	/* 9 octaves from A0 to A9 */
       double min_db	= -100.0;	/* Values below this are black */
       double ppsec	= 25.0;		/* pixel columns per second */
       double step;			/* time step per column = 1/ppsec */
static double fftfreq	= 5.0;		/* 1/fft size in seconds */
       window_function_t window_function = KAISER;
static bool gray	= FALSE;	/* Display in shades of gray? */
       bool piano_lines	= FALSE;	/* Draw lines where piano keys fall? */
       bool staff_lines	= FALSE;	/* Draw manuscript score staff lines? */
       bool guitar_lines= FALSE;	/* Draw guitar string lines? */

/* Other option flags */
       bool autoplay = FALSE;		/* -p  Start playing on startup */
       bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */
       bool fullscreen = FALSE;		/* Start up in fullscreen mode? */

/* The currently opened audio file */
static audio_file_t *	audio_file;

/* The maximum magnitude seen so far by interpolate() */
static float max = 1.0;	/* maximum magnitude value seen so far */

int
main(int argc, char **argv)
{
    char *filename;

    /* Local versions to delay setting until audio_length is known */
#define UNDEFINED (-1.0)
    double bar_left_time = UNDEFINED;
    double bar_right_time = UNDEFINED;

    /*
     * Pick up parameter values from the environment
     *
     * Variables set with garbage values are silently ignored.
     *
     * These are mostly for testing or for programming a performance.
     * Interactive users will probably use the zoom function to get more ppsec
     */
    {
	char *cp; double n;	/* Temporaries */

	if ((cp = getenv("PPSEC")) != NULL && (n = atof(cp)) > 0.0)
	    ppsec = n;

	if ((cp = getenv("DYN_RANGE")) != NULL && (n = atof(cp)) > 0.0)
	    min_db = -n;

	if ((cp = getenv("MIN_FREQ")) != NULL && (n = atof(cp)) > 0.0)
	    min_freq = n;

	if ((cp = getenv("MAX_FREQ")) != NULL && (n = atof(cp)) > 0.0)
	    max_freq = n;
    }

    for (argv++, argc--;	/* Skip program name */
	 argc > 0 && argv[0][0] == '-';
	 argv++, argc--) {
	int letter = argv[0][1];

	/* For flags that take an argument, advance argv[0] to point to it */
	switch (letter) {
	case 'w': case 'h': case 'j': case 'l': case 'r': case 'f': case 'p':
	case 'W':
	    if (argv[0][2] == '\0') {
		argv++, argc--;		/* -j3 */
	    } else {
		 argv[0] += 2;		/* -j 3 */
	    }
	    if (argc < 1 || argv[0][0] == '\0') {
		fprintf(stderr, "-%c what?\n", letter);
		exit(1);
	    }
	}

	switch (letter) {
	case 'a':
	    autoplay = TRUE;
	    break;
	case 'e':
	    exit_when_played = TRUE;
	    break;
	case 'w':
	    if ((disp_width = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-w width must be > 0\n");
		exit(1);
	    }
	    break;
	case 'h':
	    if ((disp_height = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-w height must be > 0\n");
		exit(1);
	    }
	    break;
	case 'F':
	    fullscreen = TRUE;
	    break;
	case 'j':
	    if ((max_threads = atoi(argv[0])) < 0) {
		fprintf(stderr, "-j threads must be >= 0 ?\n");
		exit(1);
	    }
	    break;
	case 'k':	/* Draw black and white lines where piano keys fall */
	    piano_lines = TRUE;
	    break;
	case 's':	/* Draw conventional score notation staff lines? */
	    staff_lines = TRUE;
	    guitar_lines = FALSE;
	    break;
	case 'g':	/* Draw guitar string lines? */
	    guitar_lines = TRUE;
	    staff_lines = FALSE;
	    break;
	case 'v':
	    printf("Version: %s\n", VERSION);
	    exit(0);
	/*
	 * Parameters that take a floating point argument
	 */
	case 'p':	/* Play starting from time t */
	case 'l': case 'r':	/* Set bar line positions */
	case 'f':	/* Set FFT frequency */
	    errno = 0;
	    {
		char *endptr;
		double arg = strtof(argv[0], &endptr);

		if (arg < 0.0 || errno == ERANGE || endptr == argv[0]) {
		    fprintf(stderr, "-%c %s must be a positive floating point value\n", letter, letter == 'f' ? "Hz" : "seconds");
		    exit(1);
		}
		switch (letter) {
		case 'p': disp_time = arg; break;
		case 'l': bar_left_time = arg; break;
		case 'r': bar_right_time = arg; break;
		case 'f':
		    if (arg <= 0.0) {
			fprintf(stderr, "-f FFT frequency must be > 0\n");
			exit(1);
		    }
		    fftfreq = arg;
		    break;
		}
		/* We can't call set_bar_*_time() until audio_length is known */
	    }
	    break;
	/*
	 * Parameters that take a string argument
	 */
	case 'W':
	    switch (tolower(argv[0][0])) {
	    case 'r': window_function = RECTANGULAR; break;
	    case 'k': window_function = KAISER; break;
	    case 'h': window_function = HANN; break;
	    case 'n': window_function = NUTTALL; break;
	    default:
		fprintf(stderr, "-W what (rectangular/kaiser/hann/nuttall)\n");
		exit(1);
	    }
	    break;

	default:
	    fprintf(stderr,
"Usage: spettro [-a] [-e] [-h n] [-w n] [-j n] [-p] [-s] [-g] [-v] [file.wav]\n\
-a:    Autoplay the file on startup\n\
-e:    Exit when the audio file has played\n\
-h n   Set spectrogram display height to n pixels\n\
-w n   Set spectrogram display width to n pixels\n\
-f n   Set the FFT frequency (default: %g Hz)\n\
-p n   Set the initial playing time in seconds\n\
-j n   Set maximum number of threads to use (default: the number of CPUs)\n\
-k     Overlay black and white lines showing frequencies of an 88-note keyboard\n\
-s     Overlay conventional score notation pentagrams as white lines\n\
-g     Overlay lines showing the positions of a classical guitar's strings\n\
-v:    Print the version of spettro that you're using\n\
-W x   Use FFT window function x where x is\n\
       r for rectangular, k for Kaiser, n for Nuttall or h for Hann\n\
If no filename is supplied, it opens \"audio.wav\"\n\
== Keyboard commands ==\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by a tenth of a screenful\n\
           Shift: by a screenful; Ctrl: by one pixel; Shift-Ctrl: by one second\n\
Up/Down    Pan up/down the frequency axis by a whole tone\n\
           (by an octave if Shift is held; by one pixel if Control is held)\n\
PgUp/PgDn  Pan up/down the frequency axis by a screenful\n\
X/x        Zoom in/out on the time axis by a factor of 2\n\
Y/y        Zoom in/out on the frequency axis by a factor of 2\n\
Plus/Minus Zoom in/out on both axes\n\
Star/Slash Change the dynamic range by 6dB to brighten/darken the quiet areas\n\
b/d        The same as star/slash (meaning \"brighter\" and \"darker\")\n\
k          Toggle overlay of 88 piano key frequencies\n\
s          Toggle overlay of conventional staff lines\n\
g          Toggle overlay of classical guitar strings' frequencies\n\
t          Show the current playing time on stdout\n\
Crtl-R     Redraw the display, should it get out of sync with the audio\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
wx         Select window function x. x works as for the -W flag\n\
Q/Ctrl-C   Quit\n\
== Environment variables ==\n\
PPSEC      Pixel columns per second, default %g\n\
MIN_FREQ   The frequency centred on the bottom pixel row, default %gHz\n\
MAX_FREQ   The frequency centred on the top pixel row, default %gHz\n\
DYN_RANGE  Dynamic range of amplitude values in decibels, default %gdB\n\
Zooms on the time axis (X,x,+,-) change PPSEC\n\
Frequency-axis zooms and pans (Up,Down,Y,y,+,-) change MIN_FREQ and MAX_FREQ\n\
Brightness controls (*,/) change DYN_RANGE\n\
", fftfreq, ppsec, min_freq, max_freq, -min_db);
	    exit(1);
	}
    }

    /* Set variables with derived values */

    disp_offset = disp_width / 2;
    step = 1 / ppsec;

    /* Set default values for unset parameters */

    filename = (argc > 0) ? argv[0] : "audio.wav";

    /* Make the row overlay mask, if any */
    make_row_overlay();

    gui_init(filename);

    green_line();

    gui_update_display();

    /* Open the audio file to find out sampling rate, length and to be able
     * to fetch pixel data to be converted into spectra.
     * Emotion seems not to let us get the raw sample data or sampling rate
     * and doesn't know the file length until the "open_done" event arrives
     * so we use libsndfile or libaudiofile for that.
     */
    if ((audio_file = open_audio_file(filename)) == NULL) goto quit;

    /* Now that we have sample_rate, we can convert fftfreq to speclen */
    speclen = fftfreq_to_speclen(fftfreq, sample_rate);

    init_audio(audio_file, filename);

    /* Apply the -p flag */
    if (disp_time != 0.0) set_playing_time(disp_time);

    /* Now we have audio_length, we can set the bar times if given... */
    if (bar_left_time != UNDEFINED) {
	if (bar_left_time > audio_length + DELTA) {
	    fprintf(stderr, "-l time is after the end of the audio\n");
	    exit(1);
	} else set_bar_left_time(bar_left_time);
    }
    if (bar_right_time != UNDEFINED) {
	if (bar_right_time > audio_length + DELTA) {
	    fprintf(stderr, "-r time is after the end of the audio\n");
	    exit(1);
	} else set_bar_right_time(bar_right_time);
    }

    /* ... and schedule the initial screen refresh */
    start_scheduler(max_threads);
    /* From here on, do not goto quit. */
    calc_columns(0, disp_width - 1);

    start_timer();

    gui_main();

quit:
    gui_deinit();

    return 0;
}

/*
 * Schedule the FFT thread(s) to calculate the results for these display columns
 */
static void
calc_columns(int from, int to)
{
    calc_t *calc = malloc(sizeof(calc_t));

    if (calc == NULL) {
	fputs("Out of memory in calc_columns()\n", stderr);
	exit(1);
    }
    calc->audio_file = audio_file;
    calc->length = audio_length;
    calc->sr	= sample_rate;
    calc->from	= disp_time + (from - disp_offset) * step;
    calc->to	= disp_time + (to - disp_offset) * step;

    /*
     * Limit start and end times to size of audio file
     */
    if (calc->from <= DELTA) calc->from = 0.0;
    if (calc->to <= DELTA) calc->to = 0.0;
    /* Last moment as a multiple of step */
    if (calc->from >= floor(audio_length / step) * step - DELTA)
	calc->from = floor(audio_length / step) * step;
    if (calc->to >= floor(audio_length / step) * step - DELTA)
	calc->to = floor(audio_length / step) * step;

    calc->ppsec  = ppsec;
    calc->speclen= speclen;
    calc->window = window_function;

    /* If for a single column, just schedule it */
    if (from == to) schedule(calc);
    else { /* otherwise, schedule each column individually */
	double t;
	/* Handle descending ranges */
	if (calc->from > calc->to) {
	    double tmp = calc->from;
	    calc->from = calc->to;
	    calc->to = tmp;
	}
	/* Schedule each column as a separate calculation */
	for (t=calc->from; t <= calc->to + DELTA; t+=step) {
	    calc_t *new = malloc(sizeof(calc_t));
	    memcpy(new, calc, sizeof(calc_t));
	    new->from = new->to = t;
	    schedule(new);
	}
	free(calc);
    }
}

/*
 * Process a keystroke.  Also inspects the variables Control and Shift.
 */
void
do_key(enum key key)
{
    static bool waiting_for_window_function = FALSE;

    if (waiting_for_window_function) {
	window_function_t new_fn = -1;
	switch (key) {
	case KEY_R: new_fn = RECTANGULAR;	break;
	case KEY_K: new_fn = KAISER;		break;
	case KEY_H: new_fn = HANN;		break;
	case KEY_N: new_fn = NUTTALL;		break;
	default:    new_fn = window_function; /* defuse the if below */
	}
	if (new_fn != window_function) {
	    window_function = new_fn;
fprintf(stderr, "Repainting displayed columns for window function %d\n", window_function);
	    repaint_display(FALSE);	/* Repaint already-displayed columns */
	}
	waiting_for_window_function = FALSE;
	return;
    }

    switch (key) {

    case KEY_NONE:	/* They pressed something else */
	break;

    case KEY_QUIT:	/* Quit */
	if (playing == PLAYING) stop_playing();
	stop_scheduler();
	stop_timer();
	gui_quit();
	break;

    case KEY_SPACE:	/* Play/Pause/Rewind */
	switch (playing) {
	case PLAYING:
	    pause_audio();
	    break;

	case STOPPED:
	    disp_time = 0.0;
	    repaint_display(FALSE);
	    start_playing();
	    break;

	case PAUSED:
	    continue_playing();
	    break;
	}
	break;

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
	set_playing_time(0.0);
	break;
    case KEY_END:
	set_playing_time(audio_length);
	break;

    /*
     * Arrow Up/Down: Pan the frequency axis by a tone.
     * With Shift: by an octave. With Control, by a pixel.
     * The argument to freq_pan_by() multiplies min_freq and max_freq.
     */
    case KEY_UP:
	freq_pan_by(Control ? exp(log(max_freq/min_freq) / (disp_height-1))  :
		    Shift ? 2.0 : pow(2.0, 1/6.0));
	break;
    case KEY_DOWN:
	freq_pan_by(Control ? 1/exp(log(max_freq/min_freq) / (disp_height-1))  :
		    Shift ? 1/2.0 : pow(2.0, -1/6.0));
	break;

    /* Page Up/Down: Pan the frequency axis by a screenful */
    case KEY_PGUP:
	freq_pan_by(max_freq/min_freq);
	break;
    case KEY_PGDN:
	freq_pan_by(min_freq/max_freq);
	break;

    /* Zoom on the time axis */
    case KEY_X:
	time_zoom_by(Shift ? 2.0 : 0.5);
	repaint_display(FALSE);
	break;

    /* Zoom on the frequency axis */
    case KEY_Y:
	freq_zoom_by(Shift ? 2.0 : 0.5);
	repaint_display(FALSE);
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
    case KEY_STAR:
	change_dyn_range(6.0);
	repaint_display(TRUE);
	break;
    case KEY_SLASH:
	change_dyn_range(-6.0);
	repaint_display(TRUE);
	break;

    /* Toggle staff/piano line overlays */
    case KEY_K:
    case KEY_S:
    case KEY_G:
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

    /* Display the current UI parameters */
    case KEY_P:
	printf("time=%g step=%g min_freq=%g max_freq=%g fftfreq=%g dyn_range=%g max=%.4g\n",
		disp_time, step, min_freq, max_freq, fftfreq, -min_db, max);
	break;

    /* Display the current playing time */
    case KEY_T:
	printf("%02d:%02d (%g seconds)\n", (int) disp_time / 60,
					   (int) disp_time % 60,
					   disp_time);
	break;

    case KEY_F:
	if (Shift) {
	   /* Increase FFT size */
	   fftfreq /= 2;
	} else {
	   /* Decrease FFT size */
	   fftfreq *= 2;
	}
	speclen = fftfreq_to_speclen(fftfreq, sample_rate);

	/* Any calcs that are currently being performed will deliver
	 * a result for the old speclen and that calculation will need
	 * rescheduling at the new speclen */
	repaint_display(FALSE);
	break;

    /* The display has got corrupted, so refresh it at the current
     * playing time. */
    case KEY_REDRAW:
	disp_time = get_playing_time();
	disp_time = lrint(disp_time / step) * step;
	repaint_display(FALSE);
	break;

    /* Set left or right bar line position to current play position */
    case KEY_L:
	set_bar_left_time(disp_time);
	break;
    case KEY_R:
	set_bar_right_time(disp_time);
	break;

    case KEY_WINDOW_FUNCTION:
	/* Next letter pressed determines choice of window function */
	waiting_for_window_function = TRUE;
	break;

    /* Keys for window function that are not already claimed */
    case KEY_H: case KEY_N: break;

    default:
	fprintf(stderr, "Bogus KEY_ number %d\n", key);
    }
}

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

    scroll_event_pending = FALSE;

    new_disp_time = get_playing_time();

    if (new_disp_time < DELTA) {
	new_disp_time = 0.0;
    }
    if (new_disp_time > audio_length - DELTA) {
	new_disp_time = audio_length;
    }

    /* Align to a multiple of 1/ppsec so that times in cached results
     * continue to match. This is usually a no-op.
     */
    new_disp_time = floor(new_disp_time / step + DELTA) * step;

    scroll_by = lrint((new_disp_time - disp_time) * ppsec);

    /*
     * Scroll the display sideways by the correct number of pixels
     * and start a calc thread for the newly-revealed region.
     *
     * (4 * scroll_by) is the number of bytes by which we scroll.
     * The right-hand columns will fill with garbage or the start of
     * the next pixel row, and the final "- (4*scroll_by)" is so as
     * not to scroll garbage from past the end of the frame buffer.
     */
    if (scroll_by != 0)
    if (abs(scroll_by) >= disp_width) {
	/* If we're scrolling by more than the display width, repaint it all */
	disp_time = new_disp_time;
	calc_columns(0, disp_width - 1);
	repaint_display(FALSE);
    } else {
	/* Otherwise, shift the overlapping region and calculate the new */
	if (scroll_by > 0) {
	    /*
	     * If the green line will remain on the screen,
	     * replace it with spectrogram data.
	     * There are disp_offset columns left of the line.
	     * If there is no result for the line, schedule its calculation
	     * as it will need to be repainted when it has scrolled.
	     */
	    if (scroll_by <= disp_offset)
		repaint_column(disp_offset, 0, disp_height-1, FALSE);

	    disp_time = new_disp_time;

	    gui_h_scroll_by(scroll_by);

	    /* Repaint the right edge */
	    {   int x;
		for (x = disp_width - scroll_by; x < disp_width; x++) {
		    repaint_column(x, 0, disp_height-1, FALSE);
		}
	    }
	}
	if (scroll_by < 0) {
	    /*
	     * If the green line will remain on the screen,
	     * replace it with spectrogram data.
	     * There are disp_width - disp_offset - 1 columns right of the line.
	     */
	    if (-scroll_by <= disp_width - disp_offset - 1)
		repaint_column(disp_offset, 0, disp_height-1, FALSE);

	    disp_time = new_disp_time;

	    gui_h_scroll_by(scroll_by);

	    /* Repaint the left edge */
	    {   int x;
		for (x = -scroll_by - 1; x >= 0; x--) {
		    repaint_column(x, 0, disp_height-1, FALSE);
		}
	    }
	}

	/* Repaint the green line */
	green_line();

	/* The whole screen has changed (well, unless there's background) */
	gui_update_display();
    }
}

/* Repaint the display.
 *
 * If "all" is TRUE, repaint every column of the display from the result cache
 * or paint it with the background color if it hasn't been calculated yet (and
 * ask for it to be calculated) or is before/after the start/end of the piece.
 *
 * If "all" if FALSE, repaint only the columns that are already displaying
 * spectral data and don't ask for anything new to be calculated.
 *   This is used when something changes that affects their appearance
 * retrospectively, like "max" or "dyn_range" changing, or vertical scrolling,
 * where there's no need to repaint background, bar lines or the green line.
 *   Rather than remember what has been displayed, we repaint on-screen columns
 * that are in the result cache and have had their magnitude spectrum calculated
 * (the conversion from linear to log and the colour assignment happens when
 * an incoming result is processed to be displayed).
 *
 * Returns TRUE if the result was found in the cache and repainted,
 *	   FALSE if it painted the background color or was off-limits.
 * The GUI screen-updating function is called by whoever called us.
 */
void
repaint_display(bool refresh_only)
{
    int x;

    for (x=disp_width - 1; x >= 0; x--) {
	if (refresh_only) {
	    /* Don't repaint bar lines or the green line */
	    if (get_col_overlay(x) != 0 || x == disp_offset) continue;
	}
	repaint_column(x, 0, disp_height-1, refresh_only);
    }
    green_line();

    gui_update_display();
}

/* Repaint a column of the display from the result cache or paint it
 * with the background color if it hasn't been calculated yet or with the
 * bar lines.
 *
 * min_y and max_y limit the repainting to just the specified rows
 * (0 and disp_height-1 to paint the whole column).
 *
 * if "refresh_only" is TRUE, we only repaint columns that are already
 * displaying spectral data; we find out if a column is displaying spectral data
 * by checking the result cache: if we have a result for that time/fftfreq,
 * it's probably displaying something.
 *
 * and we don't schedule the calculation of columns whose spectral data
 * has not been displayed yet.
 *
 * The GUI screen-updating function is called by whoever called us.
 */
void
repaint_column(int column, int min_y, int max_y, bool refresh_only)
{
    /* What time does this column represent? */
    double t = disp_time + (column - disp_offset) * step;
    result_t *r;

    if (column < 0 || column >= disp_width) {
	fprintf(stderr, "Repainting off-screen column %d\n", column);
	return;
    }

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */

    /* If the column is before/after the start/end of the piece,
     * give it the background colour */
    if (t < 0.0 - DELTA || t > audio_length + DELTA) {
	if (!refresh_only)
	    gui_paint_column(column, background);
	return;
    }

    if (refresh_only) {
	/* If there's a bar line or green line here, nothing to do */
	if (get_col_overlay(column)) return;

	/* If there's any spectral data for this column, it's probably
	 * displaying something but it might be for the wrong speclen/window.
	 * We have no way of knowing what it is displaying so force its repaint
	 * with the current parameters.
	 */
	if ((r = recall_result(t, -1, -1)) != NULL) {
	    /* There's data for this column. */
	    if (r->speclen == speclen && r->window == window_function) {
		/* Bingo! It's the right result */
		paint_column(column, min_y, max_y, r);
	    } else {
		/* Bummer! It's for something else. Repaint it. */
fprintf(stderr, "Recursing!\n");
		repaint_column(column, min_y, max_y, FALSE);
	    }
	} else {
	    /* There are no results in-cache for this column,
	     * so it can't be displaying any spectral data */
	}
    } else {
	/* If we have the right spectral data for this column, repaint it */
	if ((r = recall_result(t, speclen, window_function)) != NULL) {
	    paint_column(column, min_y, max_y, r);
	} else {
	    /* ...otherwise paint it with the background color */
	    gui_paint_column(column, background);

	    /* and if it was for a valid time, schedule its calculation */
	    if (t >= 0.0 - DELTA && t <= audio_length + DELTA) {
		calc_columns(column, column);
	    }
	}
    }
}

/* Paint a column for which we have result data.
 * pos_x is a screen coordinate.
 * min_y and max_y limit the updating to those rows (0 at the bottom).
 * The GUI screen-updating function is called by whoever called us.
 */
void
paint_column(int pos_x, int min_y, int max_y, result_t *result)
{
    float *mag;
    int maglen;
    float old_max;		/* temp to detect when it changes */
    int y;
    unsigned int ov;		/* Overlay color temp; 0 = none */

    /*
     * Apply column overlay
     */
    if ((ov = get_col_overlay(pos_x)) != 0) {
	gui_paint_column(pos_x, ov);
	return;
    }

    maglen = disp_height;
    mag = calloc(maglen, sizeof(*mag));
    if (mag == NULL) {
       fprintf(stderr, "Out of memory in paint_column.\n");
       exit(1);
    }
    old_max = max;
    max = interpolate(mag, maglen, result->spec, result->speclen,
		      min_freq, max_freq, sample_rate, min_y, max_y);
    result->mag = mag;
    result->maglen = maglen;

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    gui_lock();		/* Allow pixel-writing access */
    for (y=max_y; y>=min_y; y--) {
	/* Apply row overlay, if any, otherwise paint the pixel */
	if ( (ov = get_row_overlay(y)) != 0) {
	    unsigned char *color = (unsigned char *) &ov;
	    gui_putpixel(pos_x, y, color);
	} else {
	    unsigned char color[3];
	    colormap(20.0 * log10(mag[y] / max), min_db, color, gray);
	    gui_putpixel(pos_x, y, color);
	}
    }
    gui_unlock();

    /* and it the maximum amplitude changed, repaint the already-drawn
     * columns at the new brightness. */
    if (max != old_max) repaint_display(TRUE);
}

/* Paint the green line.
 * The GUI screen-update function is called by whoever called green_line() */
void
green_line()
{
    gui_paint_column(disp_offset, green);
}
