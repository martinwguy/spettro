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
#include <math.h>	/* for lrint() */
#include <string.h>	/* for memset() */
#include <errno.h>
#include <math.h>
#include <ctype.h>	/* for tolower() */
#if USE_LIBAV
#include "libavformat/version.h"
#endif

/*
 * Local header files
 */

#include "audio.h"
#include "audio_file.h"
#include "audio_cache.h"
#include "axes.h"
#include "barlines.h"
#include "cache.h"
#include "calc.h"
#include "colormap.h"
#include "convert.h"
#include "interpolate.h"
#include "gui.h"
#include "mouse.h"
#include "overlay.h"
#include "scheduler.h"
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
       unsigned disp_width	= 640;	/* Size of display area in pixels */
       unsigned disp_height	= 480;
       double disp_time	= 0.0;	/* When in the audio file is the crosshair? */
       int disp_offset; 	/* Crosshair is in which display column? */
       double min_freq	= 27.5;		/* Range of frequencies to display: */
       double max_freq	= 14080;	/* 9 octaves from A0 to A9 */
       double min_db	= -100.0;	/* Values below this are black */
       double fps	= 25.0;		/* Frames per second; scrolls per sec */
       double ppsec	= 25.0;		/* pixel columns per second */
       double step = 0.0;		/* time step per column = 1/ppsec
					 * 0.0 means "not set yet" as a
					 * booby trap. */
       double fft_freq	= 5.0;		/* 1/fft size in seconds */
       int speclen;			/* Size of linear spectral data */
       int maglen;			/* Size of logarithmic spectral data
					 * == height of graph in pixels */
       window_function_t window_function = KAISER;
       bool piano_lines	= FALSE;	/* Draw lines where piano keys fall? */
       bool staff_lines	= FALSE;	/* Draw manuscript score staff lines? */
       bool guitar_lines= FALSE;	/* Draw guitar string lines? */

/* Other option flags */
       bool autoplay = FALSE;		/* -p  Start playing on startup */
       bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */
       bool fullscreen = FALSE;		/* Start up in fullscreen mode? */
       int min_x, max_x, min_y, max_y;	/* Edges of graph in display coords */
       bool green_line_off = FALSE;	/* Do we repaint the green line with
					 * spectral data when refreshing? */
       double softvol = 1.0;

/* The currently opened audio file */
static audio_file_t *	audio_file;

/* The maximum magnitude seen so far by interpolate() */
static float logmax = 1.0;	/* maximum magnitude value seen so far */

       bool show_axes = FALSE;	/* Are we to show/showing the axes? */

       char *output_file = NULL; /* PNG file to write to and quit. This is done
       				  * when the last result has come in from the
				  * FFT threads, in calc_notify in scheduler.c
				  */
int
main(int argc, char **argv)
{
    char *filename;

    /* Local versions to delay setting until audio_length is known */
#define UNDEFINED (-1.0)
    double bar_left_time = UNDEFINED;
    double bar_right_time = UNDEFINED;

    for (argv++, argc--;	/* Skip program name */
	 argc > 0 && argv[0][0] == '-';
	 argv++, argc--) {
	int letter = argv[0][1];

switchagain:
	switch (letter) {
	case '-':	/* Handle long args */
	    if (!strcmp(argv[0], "--width")) argv[0] = "-w";
	    else if (!strcmp(argv[0], "--height")) argv[0] = "-h";
	    else if (!strcmp(argv[0], "--jobs")) argv[0] = "-j";
	    else if (!strcmp(argv[0], "--left-bar-line")) argv[0] = "-l";
	    else if (!strcmp(argv[0], "--right-bar-line")) argv[0] = "-r";
	    else if (!strcmp(argv[0], "--fft-freq")) argv[0] = "-f";
	    else if (!strcmp(argv[0], "--start-at")) argv[0] = "-t";
	    else if (!strcmp(argv[0], "--output-png")) argv[0] = "-o";
	    else if (!strcmp(argv[0], "--window")) argv[0] = "-W";
	    else if (!strcmp(argv[0], "--rectangular")) argv[0] = "-WR";
	    else if (!strcmp(argv[0], "--kaiser")) argv[0] = "-WK";
	    else if (!strcmp(argv[0], "--nuttall")) argv[0] = "-WN";
	    else if (!strcmp(argv[0], "--hann")) argv[0] = "-WH";
	    else if (!strcmp(argv[0], "--hamming")) argv[0] = "-WM";
	    else if (!strcmp(argv[0], "--bartlett")) argv[0] = "-WB";
	    else if (!strcmp(argv[0], "--blackman")) argv[0] = "-WL";
	    else if (!strcmp(argv[0], "--dolph")) argv[0] = "-WD";
	    else if (!strcmp(argv[0], "--heatmap")) argv[0] = "-ch";
	    else if (!strcmp(argv[0], "--gray")) argv[0] = "-cg";
	    else if (!strcmp(argv[0], "--grey")) argv[0] = "-cg";
	    else if (!strcmp(argv[0], "--print")) argv[0] = "-cp";
	    else if (!strcmp(argv[0], "--softvol")) argv[0] = "-v";
	    else if (!strcmp(argv[0], "--dyn-range")) argv[0] = "-d";
	    else if (!strcmp(argv[0], "--min-freq")) argv[0] = "-n";
	    else if (!strcmp(argv[0], "--max-freq")) argv[0] = "-x";
	    /* Boolean flags */
	    else if (!strcmp(argv[0], "--autoplay")) argv[0] = "-p";
	    else if (!strcmp(argv[0], "--exit-at-end")) argv[0] = "-e";
	    else if (!strcmp(argv[0], "--fullscreen")) argv[0] = "-F";
	    else if (!strcmp(argv[0], "--fs")) argv[0] = "-F";
	    else if (!strcmp(argv[0], "--piano")) argv[0] = "-k";
	    else if (!strcmp(argv[0], "--guitar")) argv[0] = "-g";
	    else if (!strcmp(argv[0], "--score")) argv[0] = "-s";
	    else if (!strcmp(argv[0], "--show-axes")) argv[0] = "-a";
	    /* Those environment variables */
	    else if (!strcmp(argv[0], "--fps")) argv[0] = "-S";
	    else if (!strcmp(argv[0], "--ppsec")) argv[0] = "-P";
	    else goto usage;

	    letter = argv[0][1];

	    goto switchagain;

	/* For flags that take an argument, advance argv[0] to point to it */
	case 'n': case 'x':
	case 'w': case 'h': case 'j': case 'l': case 'r': case 'f': case 't':
	case 'o': case 'W': case 'c': case 'v': case 'd': case 'S': case 'P':
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
	case 'p':
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
	case 'a':
	    show_axes = TRUE;
	    break;

	/*
	 * Parameters that take a floating point argument
	 */
	case 'n':	/* Minimum frequency */
	case 'x':	/* Maximum frequency */
	case 't':	/* Play starting from time t */
	case 'l':	/* Set left bar line position */
	case 'r':	/* Set right bar line position */
	case 'f':	/* Set FFT frequency */
	case 'v':	/* Set software volume control */
	case 'd':	/* Set dynamic range */
	case 'S':	/* Set scrolling rate */
	case 'P':	/* Set pixel columns per second */
	    errno = 0;
	    {
		char *endptr;
		double arg = strtod(argv[0], &endptr);

		if (errno == ERANGE || endptr == argv[0] || !isfinite(arg)) {
		    fprintf(stderr, "The parameter to -%c must be a floating point number%s.\n",
		    	    letter,
			    tolower(letter) == 'f' ? "in Hz" :
			    letter != 'v' ? "in seconds" :
			    "");
		    exit(1);
		}
		/* They should all be >= 0 (well, except softvol!) */
		if (arg < 0.0 && letter != 'v') {
		    fprintf(stderr, "The argument to -%c must be positive.\n",
		    	    letter);
		    exit(1);
		}
		if (arg == 0.0 && letter == 'f') {
		    fprintf(stderr, "The FFT frequency must be > 0.\n");
		    exit(1);
		}
		switch (letter) {
		case 'n': min_freq = arg;	break;
		case 'm': max_freq = arg;	break;
		case 't': disp_time = arg;	break;
		case 'l': bar_left_time = arg;	break;
		case 'r': bar_right_time = arg; break;
		case 'f': fft_freq = arg;	break;
		case 'v': softvol = arg;	break;
		case 'd': min_db = -arg;	break;
		case 'S': fps = arg;		break;
		case 'P': ppsec = arg;		break;
		}
	    }
	    break;
	/*
	 * Parameters that take a string argument
	 */
	case 'o':
	    output_file = argv[0];
	    break;

	case 'W':
	    switch (tolower(argv[0][0])) {
	    case 'r': window_function = RECTANGULAR; break;
	    case 'k': window_function = KAISER; break;
	    case 'n': window_function = NUTTALL; break;
	    case 'h': window_function = HANN; break;
	    case 'm': window_function = HAMMING; break;
	    case 'b': window_function = BARTLETT; break;
	    case 'l': window_function = BLACKMAN; break;
	    case 'd': window_function = DOLPH; break;
	    default:
		fprintf(stderr, "-W which_window_function?\n\
R = Rectangular\n\
K = Kaiser (the default)\n\
N = Nuttall\n\
H = Hann\n\
M = Hamming\n\
B = Bartlett\n\
L = Blackman\n\
D = Dolph\n");
		exit(1);
	    }
	    break;

	case 'c':			     /* Choose color palette */
	    switch (tolower(argv[0][0])) {
	    case 'h': set_colormap(HEAT_MAP); break;
	    case 'g': set_colormap(GRAY_MAP); break;
	    case 'p': set_colormap(PRINT_MAP); break;
	    default:
		fprintf(stderr, "-c which? (heat/gray/print)\n");
		exit(1);
	    }
	    break;

	default:	/* Print Usage message */
	  {
usage:
	    printf("Spettro version %s built with", VERSION);
#if USE_EMOTION || USE_EMOTION_SDL
	    printf(" Enlightenment %d.%d", EFL_VERSION_MAJOR, EFL_VERSION_MINOR);
#endif
#if USE_EMOTION_SDL
	    printf(",");
#endif
#if USE_SDL || USE_EMOTION_SDL
# if SDL1
	    printf(" SDL 1.2");
# elif SDL2
	    printf(" SDL 2.0");
# endif
#endif
	    printf(" and ");
#if USE_LIBAUDIOFILE
	    printf("libaudiofile %d.%d.%d",
		    LIBAUDIOFILE_MAJOR_VERSION,
		    LIBAUDIOFILE_MINOR_VERSION,
		    LIBAUDIOFILE_MICRO_VERSION);
#elif USE_LIBSNDFILE
	    printf("libsndfile");
#elif USE_LIBSOX
	    printf("libSoX %s", sox_version());
#elif USE_LIBAV
	    printf("FFMPEG's libav %s", AV_STRINGIFY(LIBAVFORMAT_VERSION));
#endif
	    printf("\n");
	    printf(
"Usage: spettro [options] [file]\n\
-p:    Autoplay the file on startup\n\
-e:    Exit when the audio file has played\n\
-h n   Set the window's height to n pixels, default %u\n\
-w n   Set the window's width to n pixels, default %u\n\
-F     Play in fullscreen mode\n\
-n min Set the minimum displayed frequency in Hz\n\
-x min Set the maximum displayed frequency in Hz\n\
-d n   Set the dynamic range of the color map in decibels, default %gdB\n\
-y     Label the vertical frequency axis\n\
-f n   Set the FFT frequency, default %gHz\n\
-t n   Set the initial playing time in seconds\n\
-S n   Set the scrolling rate in frames per second\n\
-P n   Set the number of pixel columns per second\n\
-j n   Set maximum number of threads to use (default: the number of CPUs)\n\
-k     Overlay black and white lines showing frequencies of an 88-note keyboard\n\
-s     Overlay conventional score notation pentagrams as white lines\n\
-g     Overlay lines showing the positions of a classical guitar's strings\n\
-v n   Set the softvolume level to N (>1.0 is louder, <1.0 is softer)\n\
-W x   Use FFT window function x where x starts with\n\
       r for rectangular, k for Kaiser, n for Nuttall or h for Hann\n\
-c map Select a color map: heatmap, gray or print\n\
-o f   Display the spectrogram, dump it to file f in PNG format and quit.\n\
If no filename is supplied, it opens \"audio.wav\"\n\
== Keyboard commands ==\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by a tenth of a screenful\n\
           Shift: by a screenful; Ctrl: by one pixel; Shift-Ctrl: by one second\n\
Up/Down    Pan up/down the frequency axis by a tenth of the graph's height\n\
           (by a screenful if Shift is held; by one pixel if Control is held)\n\
PgUp/PgDn  Pan up/down the frequency axis by a screenful, like Shift-Up/Down\n\
X/x        Zoom in/out on the time axis\n\
Y/y        Zoom in/out on the frequency axis\n\
Plus/Minus Zoom both axes\n\
c          Flip between color maps: heat map - grayscale - gray for printing\n\
Star/Slash Change the dynamic range by 6dB to brighten/darken the quiet areas\n\
b/d        The same as star/slash (meaning \"brighter\" and \"darker\")\n\
f/F        Halve/double the length of the sample taken to calculate each column\n\
R/K/N/H    Set the FFT window function to Rectangular, Kaiser, Nuttall or Hann\n\
M/B/L/D    Set the FFT window function to Hamming, Bartlett, Blackman or Dolph\n\
a          Toggle the frequency axis legend\n\
k          Toggle the overlay of 88 piano key frequencies\n\
s          Toggle the overlay of conventional staff lines\n\
g          Toggle the overlay of classical guitar strings' frequencies\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
9/0        Decrease/increase the soft volume control\n\
t          Show the current playing time on stdout\n\
Crtl-L     Redraw the display from cached FFT results\n\
Crtl-R     Empty the result cache and redraw the display from the audio data\n\
q/Ctrl-C/Esc   Quit\n\
", disp_width, disp_height,-min_db, fft_freq);
	    exit(1);
	  }
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

    /* Set variables with derived values */
    disp_offset = disp_width / 2;
    step = 1 / ppsec;
    min_x = 0; max_x = disp_width - 1;
    min_y = 0; max_y = disp_height - 1;
    if (show_axes) {
	min_x += FREQUENCY_AXIS_WIDTH;
	max_x -= NOTE_NAME_AXIS_WIDTH;
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

    if (bar_left_time != UNDEFINED) {
	if (DELTA_GT(bar_left_time, audio_length)) {
	    fprintf(stderr, "-l time is after the end of the audio\n");
	    exit(1);
	} else set_left_bar_time(bar_left_time);
    }
    if (bar_right_time != UNDEFINED) {
	if (DELTA_GT(bar_right_time, audio_length)) {
	    fprintf(stderr, "-r time is after the end of the audio\n");
	    exit(1);
	} else set_right_bar_time(bar_right_time);
    }

    start_scheduler(max_threads);

    if (show_axes) draw_frequency_axes();

    repaint_display(FALSE); /* Schedules the initial screen refresh */

    gui_update_display();

    start_timer();
    gui_main();

    if (output_file) {
	while (there_is_work()) sleep(1);
	while (jobs_in_flight > 0) usleep(100000);
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
    calc->speclen= speclen;
    calc->window = window_function;

    /*
     * Limit the range to the start and end of the audio file.
     */
    if (DELTA_LE(from, 0.0)) from = 0.0;
    if (DELTA_LE(to, 0.0)) to = 0.0;
    {
	/* End of audio file as a multiple of step */
	double last_time= floor(audio_length / step) * step;

	if (DELTA_GE(from, last_time))	from = last_time;
	if (DELTA_GE(to, last_time))	to = last_time;
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
	if (DELTA_GE(to, disp_time)) {
	    double t;
	    for (t = max(from, disp_time); DELTA_LE(t, to); t += step) {
		calc_t *new = Malloc(sizeof(calc_t));
		memcpy(new, calc, sizeof(calc_t));
		new->t = t;
		schedule(new);
	    }
	}
	/* Do any columns that are < disp_time in reverse order */
	if (DELTA_LT(from, disp_time)) {
	    double t;
	    for (t=disp_time - step; DELTA_GE(t, from); t -= step) {
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
	set_playing_time(audio_length);
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
		if (max_freq > sample_rate / 2) max_freq = sample_rate / 2;
		if (min_freq < fft_freq) min_freq = fft_freq;

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
	    min_x = 0;
	    max_x = disp_width - 1;
	    repaint_columns(0, FREQUENCY_AXIS_WIDTH-1, min_y, max_y, FALSE);
	    repaint_columns(disp_width - NOTE_NAME_AXIS_WIDTH, disp_width - 1,
	    		    min_y, max_y, FALSE);
	} else {
	    /* Add frequency axis */
	    min_x = FREQUENCY_AXIS_WIDTH;
	    max_x = disp_width - 1 - NOTE_NAME_AXIS_WIDTH;
	    draw_frequency_axes();
	}
	show_axes = !show_axes;
	gui_update_rect(0, 0, FREQUENCY_AXIS_WIDTH, disp_height);
	gui_update_rect(disp_width - NOTE_NAME_AXIS_WIDTH, 0,
			NOTE_NAME_AXIS_WIDTH, disp_height);
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

    /* Display the current UI parameters */
    case KEY_P:
	if (Shift || Control) break;
	printf("min_freq=%g max_freq=%g fft_freq=%g dyn_range=%g audio_length=%g\n",
		min_freq,   max_freq,   fft_freq,   -min_db,   audio_length);
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
	   fft_freq /= 2;
	} else {
	   /* Decrease FFT size */
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

    if (DELTA_LE(new_disp_time, 0.0)) new_disp_time = 0.0;
    if (DELTA_GE(new_disp_time, audio_length)) new_disp_time = audio_length;

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
	    if (get_col_overlay(x, NULL)) continue;
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
 * by checking the result cache: if we have a result for that time/fft_freq,
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

    if (column < min_x || column >= max_x) {
	fprintf(stderr, "Repainting off-screen column %d\n", column);
	return;
    }

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */

    /* If the column is before/after the start/end of the piece,
     * give it the background colour */
    if (DELTA_LT(t, 0.0) || DELTA_GT(t, audio_length)) {
	if (!refresh_only)
	    gui_paint_column(column, min_y, max_y, background);
	return;
    }

    if (refresh_only) {
	/* If there's a bar line or green line here, nothing to do */
	if (get_col_overlay(column, NULL)) return;

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
	color_t ov;
	if (get_col_overlay(column, &ov)) {
	    gui_paint_column(column, from_y, to_y, ov);
	} else
	/* If we have the right spectral data for this column, repaint it */
	if ((r = recall_result(t, speclen, window_function)) != NULL) {
	    paint_column(column, from_y, to_y, r);
	} else {
	    /* ...otherwise paint it with the background color */
	    gui_paint_column(column, from_y, to_y, background);

	    /* and if it was for a valid time, schedule its calculation */
	    if (DELTA_GE(t, 0.0) && DELTA_LE(t, audio_length)) {
		calc_columns(column, column);
	    }
	}
    }
}

/* Paint a column for which we have result data.
 * pos_x is a screen coordinate.
 * min_y and max_y limit the updating to those screen rows.
 * The GUI screen-updating function is called by whoever called us.
 */
void
paint_column(int pos_x, int from_y, int to_y, result_t *result)
{
    float *logmag;
    int y;
    color_t ov;		/* Overlay color */

    /*
     * Apply column overlay
     */
    if (get_col_overlay(pos_x, &ov)) {
	gui_paint_column(pos_x, from_y, to_y, ov);
	return;
    }

    assert(maglen == max_y - min_y + 1);
    logmag = Calloc(maglen, sizeof(*logmag));
    logmax = interpolate(logmag, result->spec, from_y, to_y);

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    gui_lock();		/* Allow pixel-writing access */
    for (y=from_y; y <= to_y; y++) {
        int k = y - min_y;
	/* Apply row overlay, if any, otherwise paint the pixel */
	gui_putpixel(pos_x, y,
		     get_row_overlay(y, &ov)
		     ? ov : colormap(20.0 * (logmag[k] - logmax), min_db));
    }
    gui_unlock();

    free(logmag);

    /* If the maximum amplitude changed, we should repaint the already-drawn
     * columns at the new brightness. We tried this calling repaint_display here
     * but, apart from causing a jumpy pause in the scrolling, there was worse:
     * each time logmax increased it would schedule the same columns a dozen
     * times, resulting in the same calculations being done several times and
     * the duplicate results being thrown away. The old behaviour of reshading
     * the individual columns as they pass the green line is less bad.
     */
}
