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
#include <math.h>	/* for lrint() */
#include <string.h>	/* for memset() */
#include <errno.h>
#include <math.h>
#include <ctype.h>	/* for tolower() */

/*
 * Local header files
 */

#include "audio.h"
#include "audio_file.h"
#include "audio_cache.h"
#include "axes.h"
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
#include "window.h"	/* for free_windows() */
#include "ui_funcs.h"
#include "main.h"
#include "key.h"

/*
 * Function prototypes
 */
static void
repaint_columns(int from_x, int to_x, int from_y, int to_y, bool refresh_only);

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
       double step = 0.0;		/* time step per column = 1/ppsec
					 * 0.0 means "not set yet" as a
					 * booby trap. */
       double fftfreq	= 5.0;		/* 1/fft size in seconds */
       window_function_t window_function = KAISER;
       bool piano_lines	= FALSE;	/* Draw lines where piano keys fall? */
       bool staff_lines	= FALSE;	/* Draw manuscript score staff lines? */
       bool guitar_lines= FALSE;	/* Draw guitar string lines? */

/* Other option flags */
       bool autoplay = FALSE;		/* -p  Start playing on startup */
       bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */
       bool fullscreen = FALSE;		/* Start up in fullscreen mode? */
       int min_x, max_x, min_y, max_y;
       bool green_line_off = FALSE;	/* Do we repaint the green line with
					 * spectral data when refreshing? */
       double softvol = 1.0;

/* The currently opened audio file */
static audio_file_t *	audio_file;

/* The maximum magnitude seen so far by interpolate() */
static float logmax = 1.0;	/* maximum magnitude value seen so far */

       bool yflag = FALSE;

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
	case 'W': case 'c':
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
		fprintf(stderr, "-W which? (rectangular/kaiser/hann/nuttall)\n");
		exit(1);
	    }
	    break;

	case 'c':
	    if (!strcmp(argv[0], "sox")) set_colormap(SOX_MAP);
	    else if (!strcmp(argv[0], "sndfile")) set_colormap(SNDFILE_MAP);
	    else if (!strcmp(argv[0], "gray")) set_colormap(GRAY_MAP);
	    else if (!strcmp(argv[0], "grey")) set_colormap(GRAY_MAP);
	    else if (!strcmp(argv[0], "print")) set_colormap(PRINT_MAP);
	    else {
		fprintf(stderr, "-c which? (sox/sndfile/gray/print)\n");
		exit(1);
	    }
	    break;


	case 'y':
	    yflag = TRUE;
	    break;

	default:
	    fprintf(stderr,
"Usage: spettro [options] [file.wav]\n\
-a:    Autoplay the file on startup\n\
-e:    Exit when the audio file has played\n\
-h n   Set spectrogram display height to n pixels\n\
-w n   Set spectrogram display width to n pixels\n\
-y     Label the vertical frequency axis\n\
-f n   Set the FFT frequency (default: %g Hz)\n\
-p n   Set the initial playing time in seconds\n\
-j n   Set maximum number of threads to use (default: the number of CPUs)\n\
-k     Overlay black and white lines showing frequencies of an 88-note keyboard\n\
-s     Overlay conventional score notation pentagrams as white lines\n\
-g     Overlay lines showing the positions of a classical guitar's strings\n\
-v:    Print the version of spettro that you're using\n\
-W x   Use FFT window function x where x starts with\n\
       r for rectangular, k for Kaiser, n for Nuttall or h for Hann\n\
-c map Select a color map from sox, sndfile, gray, print\n\
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
c          Flip between color maps: sox - sndfile-spectrogram\n\
Star/Slash Change the dynamic range by 6dB to brighten/darken the quiet areas\n\
b/d        The same as star/slash (meaning \"brighter\" and \"darker\")\n\
f/F        Halve/double the length of the sample taken to calculate each column\n\
R/K/N/H    Set the FFT window function to Rectangular, Kaiser, Hann or Nuttall\n\
Ctrl-Y     Toggle the frequency axis legend\n\
k          Toggle overlay of 88 piano key frequencies\n\
s          Toggle overlay of conventional staff lines\n\
g          Toggle overlay of classical guitar strings' frequencies\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
9/0        Decrease/increase the soft volume control\n\
t          Show the current playing time on stdout\n\
Crtl-L     Redraw the display from cached FFT results\n\
Crtl-R     Empty the result cache and redraw the display from the audio data\n\
q/Ctrl-C/Esc   Quit\n\
== Environment variables ==\n\
PPSEC      Pixel columns per second, default %g\n\
MIN_FREQ   The frequency centred on the bottom pixel row, default %gHz\n\
MAX_FREQ   The frequency centred on the top pixel row, default %gHz\n\
DYN_RANGE  Dynamic range of amplitude values in decibels, default %gdB\n\
", fftfreq, ppsec, min_freq, max_freq, -min_db);
	    exit(1);
	}
    }

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

    /* Initialise the graphics subsystem. */
    /* Note: SDL2 in fullcreen mode may change disp_height and disp_width */
    gui_init(filename);

    /* Set variables with derived values */
    disp_offset = disp_width / 2;
    step = 1 / ppsec;
    min_x = 0; max_x = disp_width - 1;
    min_y = 0; max_y = disp_height - 1;
    if (yflag) min_x = FREQUENCY_AXIS_WIDTH;

    make_row_overlay();

    speclen = fftfreq_to_speclen(fftfreq, sample_rate);

    init_audio(audio_file, filename);

    /* Apply the -p flag */
    if (disp_time != 0.0) set_playing_time(disp_time);

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

    start_scheduler(max_threads);

    if (yflag) draw_frequency_axis();

    repaint_display(FALSE); /* Schedules the initial screen refresh */

    gui_update_display();

    start_timer();

    gui_main();

    gui_quit();

    /* Free memory to make valgrind happier */
    drop_all_work();
    drop_all_results();
    no_audio_cache();
    free_interpolate_cache();
    free_row_overlay();
    free_windows();
    close_audio_file(audio_file);

    return 0;
}

#define max(a, b) ((a)>(b) ? (a) : (b))
#define min(a, b) ((a)<(b) ? (a) : (b))

/*
 * Schedule the FFT thread(s) to calculate the results for these display columns
 */
static void
calc_columns(int from_col, int to_col)
{
    calc_t *calc;
    /* The times represented by from_col and to_col */
    double from = disp_time + (from_col - disp_offset) * step;
    double to   = disp_time + (to_col - disp_offset) * step;;

    calc = Malloc(sizeof(calc_t));
    calc->audio_file = audio_file;
    calc->length = audio_length;
    calc->sr	= sample_rate;
    calc->ppsec  = ppsec;
    calc->speclen= speclen;
    calc->window = window_function;

    /*
     * Limit the range to the start and end of the audio file.
     */
    if (from <= DELTA) from = 0.0;
    if (to <= DELTA) to = 0.0;
    {
	/* End of audio file as a multiple of step */
	double last_time= floor(audio_length / step) * step;

	if (from >= last_time - DELTA)	from = last_time;
	if (to >= last_time - DELTA)	to = last_time;
    }

    /* If it's for a single column, just schedule it... */
    if (from_col == to_col) {
	calc->t = from;
	schedule(calc);
    } else {
	/* ...otherwise, schedule each column from "from" to "to" individually
	 * in the same order as get_work() will choose them to be calculated.
	 * If we were to schedule them in time order, and some of them are
	 * left of disp_time, then a thread calling get_work() before we have
	 * finished scheduling them would calculate and display a lone column
	 * in the left pane.
	 */

	/* Allow for descending ranges by putting "from" and "to"
	 * into ascending order */
	if (to < from) {
	    double tmp = from;
	    from = to;
	    to = tmp;
	}
	/* get_work() does first disp_time to right edge,
	 * then disp_time-1 to left edge.
	 */
	/* Columns >= disp_time */
	if (to >= disp_time - DELTA) {
	    double t;
	    for (t = max(from, disp_time); t <= to + DELTA; t += step) {
		calc_t *new = Malloc(sizeof(calc_t));
		memcpy(new, calc, sizeof(calc_t));
		new->t = t;
		schedule(new);
	    }
	}
	/* Do any columns that are < disp_time in reverse order */
	if (from < disp_time - DELTA) {
	    double t;
	    for (t=max(disp_time - step, t); t >= from - DELTA; t -= step) {
		calc_t *new = Malloc(sizeof(calc_t));
		memcpy(new, calc, sizeof(calc_t));
		new->t = t;
		schedule(new);
	    }
	}
	/* For ranges, all calc_t's we scheduled were copies of calc */
	free(calc);
    }
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
    case KEY_ESC:
    case KEY_Q:
	gui_quit_main_loop();
	break;

    case KEY_SPACE:	/* Play/Pause/Rewind */
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
     * Page Up/Down: Pan the frequency axis by (up to) a screenful
     */
    case KEY_UP:
    case KEY_DOWN:
	if (Shift && Control) break;
    case KEY_PGUP:
    case KEY_PGDN:
	if (key == KEY_UP)
	    freq_pan_by(Control ? exp(log(max_freq/min_freq)/(max_y-min_y)) :
		        Shift ? 2.0 :
			pow(2.0, 1/6.0));
	else if (key == KEY_DOWN)
	    freq_pan_by(Control ? 1/exp(log(max_freq/min_freq)/(max_y-min_y)) :
		        Shift ? 1/2.0 :
			pow(2.0, -1/6.0));
	else if (key == KEY_PGUP)
	    freq_pan_by(max_freq/min_freq);
	else if (key == KEY_PGDN)
	    freq_pan_by(min_freq/max_freq);

	if (yflag) draw_frequency_axis();
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
    /* Ctrl-Y: Toggle the frequency axis */
    case KEY_Y:
	if (Control) { /* Toggle frequency axis */
	    if (yflag) {
		/* Remove frequency axis */
		min_x = 0;
		repaint_columns(0, FREQUENCY_AXIS_WIDTH-1, min_y, max_y, FALSE);
	    } else {
		/* Add frequency axis */
		min_x = FREQUENCY_AXIS_WIDTH;
		draw_frequency_axis();
	    }
	    yflag = !yflag;
	    gui_update_rect(0, 0, FREQUENCY_AXIS_WIDTH, disp_height);
	} else {
	    freq_zoom_by(Shift ? 2.0 : 0.5);
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
	if (Shift || Control) break;
    case KEY_STAR:
	change_dyn_range(6.0);
	repaint_display(TRUE);
	break;
    case KEY_D:
	if (Shift || Control) break;
    case KEY_SLASH:
	change_dyn_range(-6.0);
	repaint_display(TRUE);
	break;

    /* Toggle staff/piano line overlays */
    case KEY_K:
	if (Shift) {
	    set_window_function(KAISER);
	    break;
	} /* else drop through */
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

    /* Display the current UI parameters */
    case KEY_P:
	if (Shift || Control) break;
	printf("min_freq=%g max_freq=%g fftfreq=%g dyn_range=%g audio_length=%g\n",
		min_freq,   max_freq,   fftfreq,   -min_db,   audio_length);
	printf("playing %g disp_time=%g step=%g %g-%g speclen=%d logmax=%g\n",
		get_playing_time(), disp_time, step,
		disp_time - disp_offset * step,
		disp_time + (disp_width - disp_offset) * step,
		speclen, logmax);
	break;

    /* Display the current playing time */
    case KEY_T:
	if (Shift || Control) break;
	printf("%02d:%02d (%g seconds)\n", (int) disp_time / 60,
					   (int) disp_time % 60,
					   disp_time);
	break;

    case KEY_F:
	if (Control) break;
	if (Shift) {
	   /* Increase FFT size */
	   fftfreq /= 2;
	} else {
	   /* Decrease FFT size */
	   if (speclen > 1)
	   fftfreq *= 2;
	}
	speclen = fftfreq_to_speclen(fftfreq, sample_rate);
	drop_all_work();

	/* Any calcs that are currently being performed will deliver
	 * a result for the old speclen, which will be ignored (or cached)
	 */
	repaint_display(FALSE);
	break;

    /* Set left or right bar line position to current play position */
    case KEY_L:
	if (!Shift && !Control) set_bar_left_time(disp_time);
	if (Control && !Shift) repaint_display(FALSE);
	break;
    case KEY_R:
	if (!Shift && !Control) set_bar_right_time(disp_time);
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

    /* softvol volume controls */
    case KEY_9:
	softvol *= 0.9;
fprintf(stderr, "Soltvol = %g\n", softvol);
	break;

    case KEY_0:
	softvol /= 0.9;
fprintf(stderr, "Soltvol = %g\n", softvol);
	break;

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
    bool scroll_forward;	/* Normal forward scroll, moving the graph left? */

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
    if (scroll_by == 0) return;

    if (abs(scroll_by) >= max_x - min_x + 1) {
	/* If we're scrolling by more than the display width, repaint it all */
	disp_time = new_disp_time;
	repaint_display(FALSE);
	return;
    }

    scroll_forward = (scroll_by > 0);
    if (scroll_by < 0) scroll_by = -scroll_by;

    /* Otherwise, shift the overlapping region and calculate the new */

    /*
     * If the green line will remain on the screen,
     * replace it with spectrogram data.
     * There are disp_offset columns left of the line.
     * If there is no result for the line, schedule its calculation
     * as it will need to be repainted when it has scrolled.
     * If logmax has changed since the column was originally painted,
     * it is repainted at a different brightness, so repaint
     */
    if (scroll_by <= scroll_forward ? (disp_offset - min_x)
				    : (max_x - disp_offset - 1) ) {
	green_line_off = TRUE;
	repaint_column(disp_offset, min_y, max_y, FALSE);
	green_line_off = FALSE;
    }

    disp_time = new_disp_time;

    gui_h_scroll_by(scroll_forward ? scroll_by : -scroll_by);

    repaint_column(disp_offset, min_y, max_y, FALSE);

    if (scroll_forward) {
	/* Repaint the right edge */
	int x;
	for (x = max_x - scroll_by; x <= max_x; x++) {
	    repaint_column(x, min_y, max_y, FALSE);
	}
    } else {
	/* Repaint the left edge */
	int x;
	for (x = min_x + scroll_by - 1; x >= min_x; x--) {
	    repaint_column(x, min_y, max_y, FALSE);
	}
    }

    /* The whole screen has changed (well, unless there's background) */
    gui_update_display();
}

/* Repaint the display.
 *
 * If "refresh_only" is FALSE, repaint every column of the display
 * from the result cache or paint it with the background color
 * if it hasn't been calculated yet (and ask for it to be calculated)
 * or is before/after the start/end of the piece.
 *
 * If "refresh_only" if TRUE, repaint only the columns that are already
 * displaying spectral data.
 *   This is used when something changes that affects their appearance
 * retrospectively, like logmax or dyn_range changing, or vertical scrolling,
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
    repaint_columns(min_x, max_x, min_y, max_y, refresh_only);

    gui_update_display();
}

static void
repaint_columns(int from_x, int to_x, int from_y, int to_y, bool refresh_only)
{
    int x;

    for (x=from_x; x <= to_x; x++) {
	if (refresh_only) {
	    /* Don't repaint bar lines or the green line */
	    if (get_col_overlay(x) != 0) continue;
	}
	repaint_column(x, min_y, max_y, refresh_only);
    }
}

/* Repaint a column of the display from the result cache or paint it
 * with the background color if it hasn't been calculated yet or with the
 * bar lines.
 *
 * from_y and to_y limit the repainting to just the specified rows
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
repaint_column(int column, int from_y, int to_y, bool refresh_only)
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
	    gui_paint_column(column, min_y, max_y, background);
	return;
    }

    if (refresh_only) {
	/* If there's a bar line or green line here, nothing to do */
	if (get_col_overlay(column)) return;

	/* If there's any result for this column in the cache, it should be
	 * displaying something, but it might be for the wrong speclen/window.
	 * We have no way of knowing what it is displaying so force its repaint
	 * with the current parameters.
	 */
	if ((r = recall_result(t, -1, -1)) != NULL) {
	    /* There's data for this column. */
	    if (r->speclen == speclen && r->window == window_function) {
		/* Bingo! It's the right result */
		paint_column(column, from_y, to_y, r);
	    } else {
		/* Bummer! It's for something else. Repaint it. */
		repaint_column(column, from_y, to_y, FALSE);
	    }
	} else {
	    /* There are no results in-cache for this column,
	     * so it can't be displaying any spectral data */
	}
    } else {
	unsigned int ov;
	if ((ov = get_col_overlay(column)) != 0) {
	    gui_paint_column(column, from_y, to_y, ov);
	} else
	/* If we have the right spectral data for this column, repaint it */
	if ((r = recall_result(t, speclen, window_function)) != NULL) {
	    paint_column(column, from_y, to_y, r);
	} else {
	    /* ...otherwise paint it with the background color */
	    gui_paint_column(column, from_y, to_y, background);

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
paint_column(int pos_x, int from_y, int to_y, result_t *result)
{
    float *logmag;
    int maglen;
    float old_max;		/* temp to detect when it changes */
    int y;
    unsigned int ov;		/* Overlay color temp; 0 = none */

    /*
     * Apply column overlay
     */
    if ((ov = get_col_overlay(pos_x)) != 0) {
	gui_paint_column(pos_x, from_y, to_y, ov);
	return;
    }

    maglen = disp_height;
    logmag = Calloc(maglen, sizeof(*logmag));
    old_max = logmax;
    logmax = interpolate(logmag, maglen, result->spec, result->speclen,
			 min_freq, max_freq, sample_rate, from_y, to_y);
    result->logmag = logmag;
    result->maglen = maglen;

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    gui_lock();		/* Allow pixel-writing access */
    for (y=from_y; y <= to_y; y++) {
	/* Apply row overlay, if any, otherwise paint the pixel */
	if ( (ov = get_row_overlay(y)) != 0) {
	    unsigned char *color = (unsigned char *) &ov;
	    gui_putpixel(pos_x, y, color);
	} else {
	    unsigned char color[3];
	    colormap(20.0 * (logmag[y] - logmax), min_db, color);
	    gui_putpixel(pos_x, y, color);
	}
    }
    gui_unlock();

    /* If the maximum amplitude changed, we should repaint the already-drawn
     * columns at the new brightness. We tried this calling repaint_display here
     * but, apart from causing a jumpy pause in the scrolling, there was worse:
     * each time logmax increased it would schedule the same columns a dozen
     * times, resulting in the same calculations being done several times and
     * the duplicate results being thrown away. The old behaviour of reshading
     * the individual columns as they pass the green line is less bad.
     */
}
