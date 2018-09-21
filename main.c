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

/* Libraries' header files. See config.h for working combinations of defines */

#if ECORE_TIMER || EVAS_VIDEO || ECORE_MAIN
#include <Ecore.h>
#include <Ecore_Evas.h>
#endif

#if EVAS_VIDEO
#include <Evas.h>
#endif

#if EMOTION_AUDIO
#include <Emotion.h>
#endif

#if SDL_AUDIO || SDL_TIMER || SDL_MAIN
# include <SDL.h>
#endif

#if SDL_MAIN
#include <X11/Xlib.h>	/* for XInitThreads() */
#include <pthread.h>
#endif

/*
 * Local header files
 */

#include "audio.h"
#include "audio_file.h"
#include "cache.h"
#include "calc.h"
#include "colormap.h"
#include "interpolate.h"
#include "key.h"
#include "mouse.h"
#include "overlay.h"
#include "scheduler.h"
#include "speclen.h"
#include "timer.h"
#include "main.h"

/*
 * Function prototypes
 */

/* Helper functions */
static void	calc_columns(int from, int to);
static void	repaint_column(int column);
static void	paint_column(int column, result_t *result);
static void	green_line(void);
static void	update_display(void);
static void	update_column(int pos_x);

/* Enlightenment's GUI callbacks */
#if EVAS_VIDEO
static void quitGUI(Ecore_Evas *ee);
#endif

/* Functions performing UI actions */
static void time_pan_by(double by);	/* Left/Right */
static void time_zoom_by(double by);	/* x/X */
static void freq_pan_by(double by);	/* Up/Down */
static void freq_zoom_by(double by);	/* y/Y */
static void change_dyn_range(double by);/* * and / */

       void do_scroll(void);

/*
 * State variables
 */

/* GUI state variables */
       int disp_width	= 640;	/* Size of displayed drawing area in pixels */
       int disp_height	= 480;
       double disp_time	= 0.0; 	/* When in the audio file is the crosshair? */
       int disp_offset;  	/* Crosshair is in which display column? */
       double min_freq	= 27.5;		/* Range of frequencies to display: */
       double max_freq	= 14080;	/* 9 octaves from A0 to A9 */
static double min_db	= -100.0;	/* Values below this are black */
static double ppsec	= 25.0;		/* pixel columns per second */
       double step;			/* time step per column = 1/ppsec */
static double fftfreq	= 5.0;		/* 1/fft size in seconds */
static bool gray	= FALSE;	/* Display in shades of gray? */
       bool piano_lines	= FALSE;	/* Draw lines where piano keys fall? */
       bool staff_lines	= FALSE;	/* Draw manuscript score staff lines? */
       bool guitar_lines= FALSE;	/* Draw guitar string lines? */

/* Other option flags */
static bool autoplay = FALSE;	/* -p  Start playing the file right away */
       bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */

/* Audio file info */
       audio_file_t *audio_file;
       double	audio_length = 0.0;	/* Length of the audio in seconds */
       double	sample_rate;		/* SR of the audio in Hertz */

/* The color for uncalculated areas:  RGB gray */
#if EVAS_VIDEO
#define background 0xFF808080
#elif SDL_VIDEO
static Uint32 background;
#endif

/* Internal data used to write on the image buffer */
#if EVAS_VIDEO
static Evas_Object *image;
static unsigned char *imagedata = NULL;
static int imagestride;		/* How many bytes per screen line ?*/
       Evas_Object *em = NULL;	/* The Emotion or Evas-Ecore object */
#endif
#if SDL_VIDEO
static SDL_Surface *screen;
#endif

/* option flags */

#if SDL_MAIN
static int get_next_SDL_event(SDL_Event *event);
#endif

int
main(int argc, char **argv)
{
#if EVAS_VIDEO
    Ecore_Evas *ee;
    Evas *canvas;
#endif
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

	if ((cp = getenv("FFTFREQ")) != NULL && (n = atof(cp)) > 0.0)
	    fftfreq = n;

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
	case 'w': case 'h': case 'j': case 'l': case 'r': case 'p':
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
	case 'p':	/* Play starting from time t */
	case 'l': case 'r':	/* Set bar line positions */
	    errno = 0;
	    {
		char *endptr;
		double arg = strtof(argv[0], &endptr);

		if (arg < 0.0 || errno == ERANGE || endptr == argv[0]) {
		    fprintf(stderr, "-%c seconds must be a positive floating point value\n", letter);
		    exit(1);
		}
		switch (letter) {
		case 'p': disp_time = arg; break;
		case 'l': bar_left_time = arg; break;
		case 'r': bar_right_time = arg; break;
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
-j n  Set maximum number of threads to use (default: the number of CPUs)\n\
-p    Overlay black and white lines showing where 88-note piano keys are\n\
-s    Overlay conventional score notation pentagrams as white lines\n\
-g    Overlay lines showing the positions of a classical guitar's strings\n\
-v:   Print the version of spettro that you're using\n\
If no filename is supplied, it opens \"audio.wav\"\n\
== Keyboard commands ==\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by one second\n\
           (by 10 seconds if Shift is held; by one pixel if Control is held)\n\
Up/Down    Pan up/down the frequency axis by a tone\n\
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
FFTFREQ    FFT audio window is 1/this, defaulting to 1/%g of a second\n\
MIN_FREQ   The frequency centred on the bottom pixel row, currently %g\n\
MAX_FREQ   The frequency centred on the top pixel row, currently %g\n\
DYN_RANGE  Dynamic range of amplitude values in decibels, default=%g\n\
Zooms on the time axis (X,x,+,-) change PPSEC\n\
Frequency-axis zooms and pans (Up,Down,Y,y,+,-) change MIN_FREQ and MAX_FREQ\n\
Brightness controls (*,/) change DYN_RANGE\n\
", ppsec, fftfreq, -min_db, min_freq, max_freq);
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

    /*
     * Initialise the various subsystems
     */
#if SDL_MAIN
    /* Without this, you get:
     * [xcb] Unknown request in queue while dequeuing
     * [xcb] Most likely this is a multi-threaded client and XInitThreads has not been called
     * [xcb] Aborting, sorry about that.
     */
    if (!XInitThreads()) {
	fprintf(stderr, "XInitThreads failed.\n");
	exit(1);
    }
#endif

#if SDL_AUDIO || SDL_TIMER || SDL_VIDEO || SDL_MAIN
    {	Uint32 flags = 0;
# if SDL_AUDIO
	flags |= SDL_INIT_AUDIO;
# endif
# if SDL_TIMER
	flags |= SDL_INIT_TIMER;
# endif
# if SDL_VIDEO
	flags |= SDL_INIT_VIDEO;
# endif
# if SDL_MAIN
	/*
	 * Maybe flags |= SDL_INIT_NOPARACHUTE to prevent it from installing
	 * signal handlers for commonly ignored fatal signals like SIGSEGV.
	 */
	flags |= SDL_INIT_EVENTTHREAD;
# endif
	if (SDL_Init(flags) != 0) {
	    fprintf(stderr, "Couldn't initialize SDL: %s.\n", SDL_GetError());
	    exit(1);
	}
	atexit(SDL_Quit);

	/* For some reason, key repeat gets disabled by default */
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
			    SDL_DEFAULT_REPEAT_INTERVAL);
    }
#endif

#if EVAS_VIDEO
    /* Initialize the graphics subsystem */
    if (!ecore_evas_init() ||
        !(ee = ecore_evas_new(NULL, 0, 0, 1, 1, NULL))) {
	fputs("Cannot initialize graphics subsystem.\n", stderr);
	exit(1);
    }
    ecore_evas_callback_delete_request_set(ee, quitGUI);
    ecore_evas_title_set(ee, "spettro");
    ecore_evas_show(ee);

    canvas = ecore_evas_get(ee);

    /* Create the image and its memory buffer */
    image = evas_object_image_add(canvas);
    evas_object_image_colorspace_set(image, EVAS_COLORSPACE_ARGB8888);
    evas_object_image_size_set(image, disp_width, disp_height);
    imagestride = evas_object_image_stride_get(image);
    imagedata = malloc(imagestride * disp_height);
    if (imagedata == NULL) {
	fprintf(stderr, "Out of memory allocating image data\n");
	exit(1);
    }
    /* Clear the image buffer to the background color */
    {	register int i;
	register unsigned int *p = (unsigned int *)imagedata;

	for (i=(imagestride * disp_height) / sizeof(*p);
	     i > 0;
	     i--) {
	    *p++ = background;
	}
    }

    evas_object_image_data_set(image, imagedata);

    /* This gives an image that is automatically scaled with the window.
     * If you resize the window, the underlying image remains of the same size
     * and it is zoomed by the window system, giving a thick green line etc.
     */
    evas_object_image_filled_set(image, TRUE);
    ecore_evas_object_associate(ee, image, 0);

    evas_object_resize(image, disp_width, disp_height);
    evas_object_focus_set(image, EINA_TRUE); /* Without this no keydown events*/

    evas_object_show(image);
#endif

#if SDL_VIDEO
    /* "Use SDL_SWSURFACE if you plan on doing per-pixel manipulations,
     * or blit surfaces with alpha channels, and require a high framerate."
     * "SDL_DOUBLEBUF is only valid when using HW_SURFACE."
     *     -- http://sdl.beuc.net/sdl.wiki/SDL_SetVideoMode
     * We could be more permissive about bpp, but 32 will do for a first hack.
     */
    screen = SDL_SetVideoMode(disp_width, disp_height, 32, SDL_SWSURFACE);
	/* | SDL_RESIZEABLE one day */
    if (screen == NULL) {
        fprintf(stderr, "Couldn't create window: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_WM_SetCaption(filename, NULL);

    background = SDL_MapRGB(screen->format, 0x80, 0x80, 0x80);

    /* Clear the image buffer to the background color */
    if (SDL_FillRect(screen, NULL, background) != 0) {
        fprintf(stderr, "Couldn't fill screen with background color: %s\n",
		SDL_GetError());
        exit(1);
    }
#endif /* SDL_VIDEO */

    green_line();
    update_display();

    /* Initialize the audio subsystem */

#if EMOTION_AUDIO || EVAS_VIDEO
# if EMOTION_AUDIO
    em = emotion_object_add(canvas);
# elif EVAS_VIDEO
    em = evas_object_smart_add(canvas, NULL);
# endif
    if (!em) {
# if EMOTION_AUDIO
	fputs("Couldn't initialize Emotion audio.\n", stderr);
# else
	fputs("Couldn't initialize Evas graphics.\n", stderr);
# endif
	exit(1);
    }
#endif

    /* Load the audio file for playing */

#if EMOTION_AUDIO
    emotion_object_init(em, NULL);
    emotion_object_video_mute_set(em, EINA_TRUE);
    if (emotion_object_file_set(em, filename) != EINA_TRUE) {
	fputs("Couldn't load audio file. Try compiling with -DUSE_EMOTION_SDL in Makefile.am\n", stderr);
	exit(1);
    }
    evas_object_show(em);
#endif

    /* Open the audio file to find out sampling rate, length and to be able
     * to fetch pixel data to be converted into spectra.
     * Emotion seems not to let us get the raw sample data or sampling rate
     * and doesn't know the file length until the "open_done" event arrives
     * so we use libsndfile or libaudiofile for that.
     */
    if ((audio_file = open_audio_file(filename)) == NULL) goto quit;
    sample_rate = audio_file_sampling_rate(audio_file);
    audio_length =
	(double) audio_file_length_in_frames(audio_file) / sample_rate;

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

#if EVAS_VIDEO
    /* Set GUI callbacks */
    evas_object_event_callback_add(image, EVAS_CALLBACK_KEY_DOWN,
				   keyDown, em);
    evas_object_event_callback_add(image, EVAS_CALLBACK_MOUSE_DOWN,
				   mouseDown, em);
#endif

    init_audio(audio_file);

    start_timer();

#if ECORE_MAIN
    /* Start main event loop */
    ecore_main_loop_begin();
#elif SDL_MAIN
    {
	SDL_Event event;
	enum key key;

	/* Prioritise UI events over window refreshes, results and such */
	while (get_next_SDL_event(&event)) switch (event.type) {

	case SDL_QUIT:
	    stop_scheduler();
	    stop_timer();
	    exit(0);	/* atexit() calls SDL_Quit() */

	case SDL_KEYDOWN:
	    Shift   = !!(event.key.keysym.mod & KMOD_SHIFT);
	    Control = !!(event.key.keysym.mod & KMOD_CTRL);
	    key = sdl_key_decode(&event);
	    if (key != KEY_NONE) do_key(key);
	    break;

	case SDL_MOUSEBUTTONDOWN:
	    {
		/* To detect Shift and Control states, it looks like we have to
		 * examine the keys ourselves */
		Uint8 *keystate = SDL_GetKeyState(NULL);
		Shift = keystate[SDLK_LSHIFT] || keystate[SDLK_RSHIFT];
		Control = keystate[SDLK_LCTRL] || keystate[SDLK_RCTRL];

		switch (event.button.button) {
		case SDL_BUTTON_LEFT:
		case SDL_BUTTON_RIGHT:
		    do_mouse(event.button.x, event.button.y,
			     event.button.button == SDL_BUTTON_LEFT
				 ? LEFT_BUTTON : RIGHT_BUTTON,
			     MOUSE_DOWN);
		}
	    }
	    break;

	case SDL_VIDEORESIZE:
	    /* One day */
	    break;

	case SDL_USEREVENT:
	    switch (event.user.code) {
	    case RESULT_EVENT:
		/* Column result from a calculation thread */
		calc_notify((result_t *) event.user.data1);
		break;
	    case SCROLL_EVENT:
		do_scroll();
		break;
	    default:
		fprintf(stderr, "Unknown SDL_USEREVENT code %d\n",
			event.user.code);
		break;
	    }
	    break;

	default:
	    break;
	}
    }
#endif

quit:
    /* Tidy up and quit.
     * You can only goto quit BEFORE starting the scheduler because
     * to cancel threads, EFL requires the main loop to be running
     */
#if EVAS_VIDEO
    ecore_evas_free(ee);
#if 0
    /* This makes it dump core or barf error messages about bad magic */
    ecore_evas_shutdown();
#endif
#endif

#if SDL_AUDIO || SDL_TIMER || SDL_VIDEO || SDL_MAIN
    SDL_Quit();
#endif

    return 0;
}

#if SDL_MAIN
static int
get_next_SDL_event(SDL_Event *eventp)
{
    /* First, see if there are any UI events to be had */
    switch (SDL_PeepEvents(eventp, 1, SDL_GETEVENT,
			  SDL_EVENTMASK(SDL_QUIT) |
			  SDL_EVENTMASK(SDL_KEYDOWN) |
			  SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN))) {
    case -1:
	fprintf(stderr, "Some error from SDL_PeepEvents().\n");
	return 0;
    case 0:
	break;
    case 1:
	return 1;
    default:
	fprintf(stderr, "Wierd return from SDL_PeepEvents\n");
    }

    /* No UI events? Wait for all events */
    return SDL_WaitEvent(eventp);
}
#endif

/*
 * Schedule the FFT thread(s) to calculate the results for these display columns
 */
static void
calc_columns(int from, int to)
{
    calc_t *calc = malloc(sizeof(calc_t));
#if ECORE_MAIN
    Ecore_Thread *thread;
#endif

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
 *	GUI callbacks
 */

#if ECORE_MAIN
/* Quit on window close or Control-Q */
static void
quitGUI(Ecore_Evas *ee EINA_UNUSED)
{
    ecore_main_loop_quit();
}
#endif

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
#if ECORE_MAIN
	ecore_main_loop_quit();
#elif SDL_MAIN
	exit(0);	/* atexit() calls SDL_Quit() */
#endif
	break;

    case KEY_SPACE:	/* Play/Pause/Rewind */
	switch (playing) {
	case PLAYING:
	    pause_playing();
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

    /* Zoom on the time axis */
    case KEY_X:
	time_zoom_by(Shift ? 2.0 : 0.5);
	break;

    /* Zoom on the frequency axis */
    case KEY_Y:
	freq_zoom_by(Shift ? 2.0 : 0.5);
	break;

    /* Normal zoom-in zoom-out, i.e. both axes. */
    case KEY_PLUS:
	freq_zoom_by(2.0);
	time_zoom_by(2.0);
	break;
    case KEY_MINUS:
	freq_zoom_by(0.5);
	time_zoom_by(0.5);
	break;

    /* Change dynamic range of color spectrum, like a brightness control.
     * Star should brighten the dark areas, which is achieved by increasing
     * the dynrange;
     * Slash instead darkens them to reduce visibility of background noise.
     */
    case KEY_STAR:
	change_dyn_range(6.0);
	break;
    case KEY_SLASH:
	change_dyn_range(-6.0);
	break;

    /* Toggle staff/piano line overlays */
    case KEY_P:
    case KEY_S:
    case KEY_G:
	if (key == KEY_P)
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
 * Jump forwards or backwards in time, scrolling the display accordingly.
 */
static void
time_pan_by(double by)
{
    double playing_time;

    playing_time = disp_time + by;
    if (playing_time < 0.0) playing_time = 0.0;
    if (playing_time > audio_length) {
	playing_time = audio_length;
	if (playing == PLAYING) {
#if EMOTION_AUDIO
            emotion_object_play_set(em, EINA_FALSE);
#endif
#if SDL_AUDIO
	    SDL_PauseAudio(1);
#endif
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
static void
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
static void
freq_pan_by(double by)
{
    min_freq *= by;
    max_freq *= by;
    repaint_display();
}

/* Zoom the frequency axis by a factor, staying centred on the centre.
 * Values > 1.0 zoom in; values < 1.0 zoom out.
 */
static void
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
static void
change_dyn_range(double by)
{
    /* As min_db is negative, subtracting from it makes it bigger */
    min_db -= by;

    /* min_db should not go positive */
    if (min_db > -6.0) min_db = -6.0;

    repaint_display();
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

	    /* Usual case: scrolling the display left to advance in time */
#if EVAS_VIDEO
	    memmove(imagedata, imagedata + (4 * scroll_by),
		    imagestride * disp_height - (4 * scroll_by));
#elif SDL_VIDEO
	    {
		SDL_Rect from, to;
		int err;

		from.x = scroll_by; to.x = 0;
		from.y = to.y = 0;
		from.w = disp_width - scroll_by;    /* to.[wh] are ignored */
		from.h = disp_height;

		if ((err = SDL_BlitSurface(screen, &from, screen, &to)) != 0) {
		    fprintf(stderr, "SDL Blit failed with value %d.\n", err);
		}
	    }
#endif

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

#if EVAS_VIDEO
	    /* Happens when they seek back in time */
	    memmove(imagedata + (4 * -scroll_by), imagedata,
		    imagestride * disp_height - (4 * -scroll_by));
#elif SDL_VIDEO
	    {
		SDL_Rect from, to;
		int err;

		from.x = 0; to.x = -scroll_by;
		from.y = to.y = 0;
		from.w = disp_width - -scroll_by;    /* to.[wh] are ignored */
		from.h = disp_height;

		if ((err = SDL_BlitSurface(screen, &from, screen, &to)) != 0) {
		    fprintf(stderr, "SDL Blit failed with value %d.\n", err);
		}
	    }
#endif

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
	update_display();
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

    update_display();
}

#if SDL_VIDEO
/* Macro derived from http://sdl.beuc.net/sdl.wiki/Pixel_Access's putpixel() */
#define putpixel(surface, x, y, pixel) \
	((Uint32 *)((Uint8 *)surface->pixels + (y) * surface->pitch))[x] = pixel
#endif

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

    /* The already-calculated result */
    result_t *r;

    if (column < 0 || column >= disp_width) {
	fprintf(stderr, "Repainting column %d\n", column);
	return;
    }

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */
    if (t >= 0.0 - DELTA && t <= audio_length + DELTA &&
        (r = recall_result(t)) != NULL) {
	paint_column(column, r);
    } else {
	/* ...otherwise paint it with the background color */
#if EVAS_VIDEO
	int y;
	unsigned int *p = (unsigned int *)imagedata + column;

	for (y=disp_height - 1; y >= 0; y--) {
            *p = background;
	    p += imagestride / sizeof(*p);
	}
#elif SDL_VIDEO
	SDL_Rect rect = {
	    column, 0,
	    1, disp_height
	};

	SDL_FillRect(screen, &rect, background);
#endif

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
static void
paint_column(int pos_x, result_t *result)
{
    float *mag;
    int maglen;
    static float max = 1.0;	/* maximum magnitude value seen so far */
    int y;
    unsigned int ov;		/* Overlay color temp; 0 = none */

    /*
     * Apply column overlay
     */
    if ((ov = get_col_overlay(pos_x)) != 0) {
#if EVAS_VIDEO
	unsigned char *p;	/* pointer to pixel to set */

	for (y=disp_height-1,
	     p = (unsigned char *)((unsigned int *)imagedata + pos_x);
	     y >= 0;
	     y--, p += imagestride) {
		*(unsigned int *)p = ov;
	}
#elif SDL_VIDEO
	SDL_Rect rect = {
	    pos_x, 0,
	    1, disp_height
	};

	SDL_FillRect(screen, &rect, ov);
#endif
	return;
    }

    maglen = disp_height;
    mag = calloc(maglen, sizeof(*mag));
    if (mag == NULL) {
       fprintf(stderr, "Out of memory in paint_column.\n");
       exit(1);
    }
    max = interpolate(mag, maglen, result->spec, result->speclen,
		     min_freq, max_freq, sample_rate);
    result->mag = mag;
    result->maglen = maglen;

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
#if EVAS_VIDEO
    for (y=maglen-1; y>=0; y--) {
	unsigned int *pixelrow;

	pixelrow = (unsigned int *)&imagedata[imagestride * ((disp_height - 1) - y)];

	/*
	 * Apply row overlay
	 */
	if ( (ov = get_row_overlay(y)) != 0) {
	    pixelrow[pos_x] = ov;
	    continue;
	}

# if LITTLE_ENDIAN	/* Provided by stdlib.h on Linux-glibc */
	/* Let colormap write directly to the pixel buffer */
	colormap(20.0 * log10(mag[y] / max), min_db,
		 (unsigned char *)(pixelrow + pos_x), gray);
# else
	/* colormap writes to color[] and we swap them to the pixel buffer */
	{   unsigned char color[3];
	    colormap(20.0 * log10(mag[i] / max), min_db, color, gray);
	    pixelrow[pos_x] = (color[0]) | (color[1] << 8) |
-                             (color[2] << 16) | 0xFF000000;
	}
# endif
    }
#elif SDL_VIDEO
    if (SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) != 0 ) {
	fprintf(stderr, "Can't lock screen: %s\n", SDL_GetError());
	return;
    }
    for (y=maglen-1; y>=0; y--) {
	unsigned char color[3];

	if ( (ov = get_row_overlay(y)) != 0) {
	    /* SDL has y=0 at top */
	    putpixel(screen, pos_x, (disp_height-1) - y, ov);
	    continue;
	}

	colormap(20.0 * log10(mag[y] / max), min_db, color, gray);
	/* SDL has y=0 at top, and colormap returns BGR */
	putpixel(screen, pos_x, (disp_height-1) - y,
		 SDL_MapRGB(screen->format, color[2], color[1], color[0]));
    }
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
#endif
}

/* Paint the green line.
 * The GUI screen-update function is called by whoever called green_line() */
static void
green_line()
{
#if EVAS_VIDEO
    int y;
    unsigned int *p = (unsigned int *)imagedata + disp_offset;

    for (y=disp_height - 1; y >= 0; y--) {
	*p = 0xFF00FF00;
	p += imagestride / sizeof(*p);
    }
#elif SDL_VIDEO
    SDL_Rect rect = {
	disp_offset, 0,
	1, disp_height
    };

    SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, 0, 0xFF, 0));
#endif
}

/* Tell the video subsystem to update the display from the pixel data */
static void
update_display(void)
{
#if EVAS_VIDEO
	evas_object_image_data_update_add(image, 0, 0, disp_width, disp_height);
#elif SDL_VIDEO
	SDL_UpdateRect(screen, 0, 0, 0, 0);
#endif
}

/* Tell the video subsystem to update one column of the display
 * from the pixel data
 */
static void
update_column(int pos_x)
{
#if EVAS_VIDEO
    evas_object_image_data_update_add(image, pos_x, 0, 1, disp_height);
#elif SDL_VIDEO
    SDL_UpdateRect(screen, pos_x, 0, 1, disp_height);
#endif
}

/*
 *	Interface to FFT calculator
 */

/* The function called as the body of the FFT-calculation threads.
 *
 * Get work from the scheduler, do it, call the result callback and repeat.
 * If get_work() returns NULL, there is nothing to do, so sleep a little.
 */
#if ECORE_MAIN
void
calc_heavy(void *data, Ecore_Thread *thread)
#elif SDL_MAIN
void *
calc_heavy(void *data)
#endif
{
    /* The main loop of each calculation thread */
#if ECORE_MAIN
    /* Loop until this thread is scheduled to be cancelled */
    while (ecore_thread_check(thread) == FALSE) {
#elif SDL_MAIN
    int oldtype;
    if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype)) {
	fprintf(stderr, "calc_heavy cannot set thread cancel type.\n");
    }
    while (TRUE) {
#endif
	calc_t *work;

	if ((work = get_work()) == NULL) {
	    /* Sleep for a tenth of a second */
	    usleep((useconds_t)100000);
	} else {
#if ECORE_MAIN
	    work->thread = thread;
#endif
	    calc(work);
	}
    }
}

/* The callback called by calculation threads to report a result */
void
calc_result(result_t *result)
{
    /* Send result back to main loop */
    if (result != NULL)
#if ECORE_MAIN
	ecore_thread_feedback(result->thread, result);
#elif SDL_MAIN
    {
	SDL_Event event;
	event.type = SDL_USEREVENT;
	event.user.code = RESULT_EVENT;
	event.user.data1 = result;
	if (SDL_PushEvent(&event) != 0) {
	    /* The Event queue is full. let it empty and try again. */
	    usleep(100000);
	    if (SDL_PushEvent(&event) != 0) {
		fprintf(stderr, "Couldn't post a result event\n");
		return;
	    }
	}
    }
#endif
}

void
#if ECORE_MAIN
calc_notify(void *data, Ecore_Thread *thread, void *msg_data)
#elif SDL_MAIN
calc_notify(result_t *result)
#endif
{
#if ECORE_MAIN
    result_t *result = (result_t *)msg_data;
#endif
    int pos_x;	/* Where would this column appear in the displayed region? */

    /* The Evas image that we need to write to */

    /* What screen coordinate does this result correspond to? */
    pos_x = lrint(disp_offset + (result->t - disp_time) * ppsec);

    /* Update the display if the column is in the displayed region
     * and isn't at the green line's position
     */
    if (pos_x >= 0 && pos_x < disp_width &&
	pos_x != disp_offset) {
	paint_column(pos_x, result);
	update_column(pos_x);
    }

    remember_result(result);

    /* To avoid an embarassing pause at the start of the graphics, we wait
     * until the FFT delivers its first result before starting the player.
     */
    if (autoplay && playing != PLAYING) {
	start_playing();
    }
}
