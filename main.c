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
#include "key.h"
#include "mouse.h"
#include "overlay.h"
#include "scheduler.h"
#include "speclen.h"
#include "timer.h"
#include "ui_funcs.h"
#include "main.h"

/*
 * Function prototypes
 */

/* Helper functions */
static void	calc_columns(int from, int to);
static void	repaint_column(int column);
static void	green_line(void);

       void do_scroll(void);

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
static bool gray	= FALSE;	/* Display in shades of gray? */
       bool piano_lines	= FALSE;	/* Draw lines where piano keys fall? */
       bool staff_lines	= FALSE;	/* Draw manuscript score staff lines? */
       bool guitar_lines= FALSE;	/* Draw guitar string lines? */

/* Other option flags */
       bool autoplay = FALSE;	/* -p  Start playing the file on startup */
       bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */

/* The currently opened audio file */
static audio_file_t *	audio_file;

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
	case 'v':
	    printf("Version: %s\n", VERSION);
	    exit(0);
	default:
	    fprintf(stderr,
"Usage: spettro [-a] [-e] [-h n] [-w n] [-j n] [-p] [-s] [-g] [-v] [file.wav]\n\
-a:    Autoplay the file on startup\n\
-e:    Exit when the audio file has played\n\
-h n  Set spectrogram display height to n pixels\n\
-w n  Set spectrogram display width to n pixels\n\
-f n  Set the FFT frequency (default: %g Hz)\n\
-j n  Set maximum number of threads to use (default: the number of CPUs)\n\
-k    Overlay black and white lines showing frequencies of an 88-note keyboard\n\
-s    Overlay conventional score notation pentagrams as white lines\n\
-g    Overlay lines showing the positions of a classical guitar's strings\n\
-v:   Print the version of spettro that you're using\n\
If no filename is supplied, it opens \"audio.wav\"\n\
== Keyboard commands ==\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by one second\n\
           (by 10 seconds if Shift is held; by one pixel if Control is held)\n\
Up/Down    Pan up/down the frequency axis by a whole tone\n\
           (by an octave if Shift is held; by one pixel if Control is held)\n\
X/x        Zoom in/out on the time axis by a factor of 2\n\
Y/y        Zoom in/out on the frequency axis by a factor of 2\n\
Plus/Minus Zoom in/out on both axes\n\
Star/Slash Change the dynamic range by 6dB to brighten/darken the quiet areas\n\
p          Toggle overlay of piano key frequencies\n\
s          Toggle overlay of conventional staff lines\n\
g          Toggle overlay of classical guitar string frequencies\n\
t          Show the current playing time on stdout\n\
Crtl-R     Redraw the display, should it get out of sync with the audio\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
Q/Ctrl-C   Quit\n\
== Environment variables ==\n\
PPSEC      Pixel columns per second, default %g\n\
MIN_FREQ   The frequency centred on the bottom pixel row, currently %g\n\
MAX_FREQ   The frequency centred on the top pixel row, currently %g\n\
DYN_RANGE  Dynamic range of amplitude values in decibels, default=%g\n\
Zooms on the time axis (X,x,+,-) change PPSEC\n\
Frequency-axis zooms and pans (Up,Down,Y,y,+,-) change MIN_FREQ and MAX_FREQ\n\
Brightness controls (*,/) change DYN_RANGE\n\
", fftfreq, ppsec, -min_db, min_freq, max_freq);
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
    calc->speclen= fftfreq_to_speclen(fftfreq, sample_rate);
    calc->window = KAISER;

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
	    repaint_display();
	    start_playing();
	    break;

	case PAUSED:
	    continue_playing();
	    break;
	}
	break;

    /*
     * Arrow <-/->: Jump back/forward a second.
     * With Shift, 10 seconds. With Control one pixel.
     */
    case KEY_LEFT:
	time_pan_by(-(Control ? step : Shift ? 10.0 : 1.0));
	break;
    case KEY_RIGHT:
	time_pan_by(Control ? step : Shift ? 10.0 : 1.0);
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
	repaint_display();
	break;
    case KEY_DOWN:
	freq_pan_by(Control ? 1/exp(log(max_freq/min_freq) / (disp_height-1))  :
		    Shift ? 1/2.0 : pow(2.0, -1/6.0));
	repaint_display();
	break;

    /* Zoom on the time axis */
    case KEY_X:
	time_zoom_by(Shift ? 2.0 : 0.5);
	repaint_display();
	break;

    /* Zoom on the frequency axis */
    case KEY_Y:
	freq_zoom_by(Shift ? 2.0 : 0.5);
	repaint_display();
	break;

    /* Normal zoom-in zoom-out, i.e. both axes. */
    case KEY_PLUS:
	freq_zoom_by(2.0);
	time_zoom_by(2.0);
	repaint_display();
	break;
    case KEY_MINUS:
	freq_zoom_by(0.5);
	time_zoom_by(0.5);
	repaint_display();
	break;

    /* Change dynamic range of color spectrum, like a brightness control.
     * Star should brighten the dark areas, which is achieved by increasing
     * the dynrange;
     * Slash instead darkens them to reduce visibility of background noise.
     */
    case KEY_STAR:
	change_dyn_range(6.0);
	repaint_display();
	break;
    case KEY_SLASH:
	change_dyn_range(-6.0);
	repaint_display();
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
	repaint_display();
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
	repaint_display();
	break;

    /* The display has got corrupted, so refresh it at the current
     * playing time. */
    case KEY_REDRAW:
	disp_time = get_playing_time();
	disp_time = lrint(disp_time / step) * step;
	repaint_display();
	break;

    /* Set left or right bar line position to current play position */
    case KEY_BAR_START:
	set_bar_left_time(disp_time);
	break;
    case KEY_BAR_END:
	set_bar_right_time(disp_time);
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
	repaint_display();
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
		repaint_column(disp_offset);

	    disp_time = new_disp_time;

	    gui_scroll_by(scroll_by);

	    /* Repaint the right edge */
	    {   int x;
		for (x = disp_width - scroll_by; x < disp_width; x++) {
		    repaint_column(x);
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
		repaint_column(disp_offset);

	    disp_time = new_disp_time;

	    gui_scroll_by(scroll_by);

	    /* Repaint the left edge */
	    {   int x;
		for (x = -scroll_by - 1; x >= 0; x--) {
		    repaint_column(x);
		}
	    }
	}

	/* Repaint the green line */
	green_line();

	/* The whole screen has changed (well, unless there's background) */
	gui_update_display();
    }
}

/* Repaint the whole display */
void
repaint_display(void)
{
    int pos_x;

    for (pos_x=disp_width - 1; pos_x >= 0; pos_x--) {
	repaint_column(pos_x);
    }
    green_line();

    gui_update_display();
}

/* Repaint a column of the display from the result cache or paint it
 * with the background color if it hasn't been calculated yet.
 * Returns TRUE if the result was found in the cache and repainted,
 *	   FALSE if it painted the background color or was off-limits.
 * The GUI screen-updating function is called by whoever called us.
 */
static void
repaint_column(int column)
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
    if (t >= 0.0 - DELTA && t <= audio_length + DELTA &&
	(r = recall_result(t, fftfreq_to_speclen(fftfreq, sample_rate)))) {
	    paint_column(column, r);
    } else {
	/* ...otherwise paint it with the background color */
	gui_background(column);

	/* and if it was for a valid time, schedule its calculation */
	if (t >= 0.0 - DELTA && t <= audio_length + DELTA) {
	    calc_columns(column, column);
	}
    }
}

/* Paint a column for which we have result data.
 * pos_x is a screen coordinate.
 * The GUI screen-updating function is called by whoever called us.
 */
void
paint_column(int pos_x, result_t *result)
{
    float *mag;
    int maglen;
    static float max = 1.0;	/* maximum magnitude value seen so far */
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
		      min_freq, max_freq, sample_rate);
    result->mag = mag;
    result->maglen = maglen;

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    gui_lock();
    for (y=maglen-1; y>=0; y--) {

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
    if (max != old_max) repaint_display();
}

/* Paint the green line.
 * The GUI screen-update function is called by whoever called green_line() */
static void
green_line()
{
    gui_paint_column(disp_offset, green);
}
