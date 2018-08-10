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
 * mouse position.
 *
 * Mouse click and drag should pan the display in real time.
 *
 * So it should be Control-Mouse-Down that positions the left or right bar line
 * at the mouse position. The bar line should appear when you press the button
 * and if you move it while holding the button, the bar line should move too,
 * being positioned definitively when you release the mouse button.
 * If they release Control before MouseUp, no change should be made.
 *
 *	Martin Guy <martinwguy@gmail.com>, Dec 2016 - May 2017.
 */

#include "config.h"

/* System header files */

#include <stdlib.h>
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
# include <SDL/SDL.h>
#endif

/*
 * Local header files
 */

#include "audiofile.h"
#include "calc.h"
#include "interpolate.h"
#include "colormap.h"
#include "speclen.h"
#include "spettro.h"

/*
 * Function prototypes
 */

/* Helper functions */
static void	calc_columns(int from, int to);
static void	remember_result(result_t *result);
static result_t *recall_result(double t);
static void	destroy_result(result_t *r);
static void	repaint_display(void);
static void	repaint_column(int column);
static void	paint_column(int column, result_t *result);
static void	green_line(void);
static void	update_display(void);
static void	update_column(int pos_x);

/* Enlightenment's GUI callbacks */
#if EVAS_VIDEO
static void keyDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void mouseDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void quitGUI(Ecore_Evas *ee);
#endif

/* Audio playing functions */
static void pause_playing();
static void start_playing();
static void stop_playing();
static void continue_playing();
static void time_pan_by(double by);	/* Left/Right */
static void time_zoom_by(double by);	/* x/X */
static void freq_pan_by(double by);	/* Up/Down */
static void freq_zoom_by(double by);	/* y/Y */
static void change_dyn_range(double by);/* * and / */

/* The timer and its callback function. */
#if ECORE_TIMER
static Ecore_Timer *timer = NULL;
static Eina_Bool timer_cb(void *data);	/* The timer callback function */
static int scroll_event;   /* Our user-defined event to activate scrolling */
/* The function that does the actual scrolling */
static Eina_Bool scroll_cb(void *data, int type, void *event);
#elif SDL_TIMER
static SDL_TimerID timer = NULL;
static Uint32 timer_cb(Uint32 interval, void *data);
#else
# error "Define ECORE_TIMER or SDL_TIMER"
#endif

static void do_scroll(void);

/* The audio player and its callback function */
#if EMOTION_AUDIO
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);
#elif SDL_AUDIO
static void sdl_fill_audio(void *userdata, Uint8 *stream, int len);
static unsigned sdl_start = 0;	/* At what offset in the audio file, in frames,
				 * will we next read samples to play? */
#else
# error "Define EMOTION_AUDIO or SDL_AUDIO"
#endif

/* FFT calculating thread functions and callbacks */
#if ECORE_MAIN
static void calc_heavy(void *data, Ecore_Thread *thread);
static void calc_notify(void *data, Ecore_Thread *thread, void *msg_data);
static void calc_end(void *data, Ecore_Thread *thread);
static void calc_cancel(void *data, Ecore_Thread *thread);
static void calc_stop(void);
#elif SDL_MAIN
static void *calc_heavy(void *data);
static void calc_notify(result_t *result);
#else
# error "Define ECORE_MAIN or SDL_MAIN"
#endif
static void calc_result(result_t *result);

/* Overlays */
static void		make_row_overlay(void);
static unsigned int	get_row_overlay(int y);
static void		set_col_overlay_left(double when);
static void		set_col_overlay_right(double when);
static unsigned int	get_col_overlay(int y);

/*
 * State variables
 */

/* GUI state variables */
static int disp_width	= 640;	/* Size of displayed drawing area in pixels */
static int disp_height	= 480;
static double disp_time	= 0.0; 	/* When in the audio file is the crosshair? */
static int disp_offset;  	/* Crosshair is in which display column? */
static double min_freq	= 27.5;		/* Range of frequencies to display: */
static double max_freq	= 14080;	/* 9 octaves from A0 to A9 */
static double min_db	= -100.0;	/* Values below this are black */
static double ppsec	= 25.0;		/* pixel columns per second */
static double step;			/* time step per column = 1/ppsec */
static double fftfreq	= 5.0;		/* 1/fft size in seconds */
static bool gray	= FALSE;	/* Display in shades of gray? */
static bool piano_lines	= FALSE;	/* Draw lines where piano keys fall? */
static bool staff_lines	= FALSE;	/* Draw manuscript score staff lines? */
static bool guitar_lines= FALSE;	/* Draw guitar string lines? */

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
static Evas_Object *em = NULL;	/* The Emotion or Evas-Ecore object */
#endif
#if SDL_VIDEO
static SDL_Surface *screen;
#endif

/* What the audio subsystem is doing:
 * STOPPED means it has reached the end of the piece and stopped automatically
 * PLAYING means it should be playing audio,
 * PAUSED  means we've paused it or it hasn't started playing yet
 */
static enum { STOPPED, PLAYING, PAUSED } playing = PAUSED;

static audio_file_t *audio_file;
static double	audio_length = 0.0;	/* Length of the audio in seconds */
static double	sample_rate;		/* SR of the audio in Hertz */

/* option flags */
static bool autoplay = FALSE;	/* -p  Start playing the file right away */
static bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */

/* Driver-independent keypress names and modifiers */
enum key {
    KEY_NONE,
    KEY_QUIT,
    KEY_SPACE,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_X,
    KEY_Y,
    KEY_PLUS,
    KEY_MINUS,
    KEY_STAR,
    KEY_SLASH,
    KEY_P,
    KEY_S,
    KEY_G,
    KEY_BAR_START,
    KEY_BAR_END,
};
static bool Shift, Control;
static void do_key(enum key);

/* State variables */

/* When they press arrow left or right to seek, we just increment pending_seek;
 * the screen updating is then done in response to the next timer callback.
 */
static double pending_seek = 0.0;

/*
 * FFT result cache
 */
static result_t *results = NULL; /* Linked list of result structures */
static result_t *last_result = NULL; /* Last element in the linked list */

int
main(int argc, char **argv)
{
#if EVAS_VIDEO
    Ecore_Evas *ee;
    Evas *canvas;
#endif
    char *filename;
    double col_overlay_left_time = -1.0;	/* Undefined */
    double col_overlay_right_time = -1.0;	/* Undefined */

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
	switch (argv[0][1]) {
	case 'p':
	    autoplay = TRUE;
	    break;
	case 'e':
	    exit_when_played = TRUE;
	    break;
	case 'w':
	    argv++; argc--;	 /* Advance to numeric argument */
	    if (argc == 0 || (disp_width = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-w what?\n");
		exit(1);
	    }
	    break;
	case 'h':
	    argv++; argc--;	 /* Advance to numeric argument */
	    if (argc == 0 || (disp_height = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-h what?\n");
		exit(1);
	    }
	    break;
	case 'j':
	    argv++; argc--;	 /* Advance to numeric argument */
	    if (argc == 0 || (max_threads = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-j what?\n");
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
	case 'l': case 'r':
	    if (argc < 2) {
lwhat:		fprintf(stderr, "-%c what?\n", argv[0][1]);
		exit(1);
	    }
	    errno = 0;
	    {
		char *endptr;
		double arg = strtof(argv[0], &endptr);

		if (errno == ERANGE || endptr == argv[0]) goto lwhat;
		switch (argv[0][1]) {
		case 'l': col_overlay_left_time = arg; break;
		case 'r': col_overlay_right_time = arg; break;
		}
	    }
	    argv++; argc--;	 /* Consume numeric argument */
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
Star/Slash Change the dynamic range by 6dB to brighten/darken the quieter areas\n\
p	   Toggle overlay of piano key frequencies\n\
s	   Toggle overlay of conventional staff lines\n\
g          Toggle overlay of classical guitar string frequencies\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
Ctrl-Q/Ctrl-C   Quit\n\
== Environment variables ==\n\
PPSEC      Pixel columns per second, default %g\n\
FFTFREQ    FFT audio window is 1/this, defaulting to 1/%g of a second\n\
DYN_RANGE  Dynamic range of amplitude values in decibels, default=%g\n\
MIN_FREQ   The frequency centred on the bottom pixel row, currently %g\n\
MAX_FREQ   The frequency centred on the top pixel row, currently %g\n\
", ppsec, fftfreq, -min_db, min_freq, max_freq);
	    exit(1);
	}
    }

    /* Set variables with derived values */

    disp_offset = disp_width / 2;
    step = 1 / ppsec;

    if (col_overlay_left_time != -1.0)
	set_col_overlay_left(col_overlay_left_time);
    if (col_overlay_right_time != -1.0)
	set_col_overlay_right(col_overlay_right_time);

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

    /* Fiddle with ecore's settings */
    if (max_threads > 0) ecore_thread_max_set(max_threads);

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
    green_line();
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

    green_line();

    update_display();
#endif /* SDL_VIDEO */

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
	fputs("Couldn't load audio file.\n", stderr);
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

#if EVAS_VIDEO
    /* Set GUI callbacks */
    evas_object_event_callback_add(image, EVAS_CALLBACK_KEY_DOWN,
				   keyDown, em);
    evas_object_event_callback_add(image, EVAS_CALLBACK_MOUSE_DOWN,
				   mouseDown, em);
#endif

#if EMOTION_AUDIO
    /* Set audio player callbacks */
    evas_object_smart_callback_add(em, "playback_finished",
				   playback_finished_cb, NULL);
#endif

#if SDL_AUDIO
    {
	SDL_AudioSpec wavspec;

	wavspec.freq = lrint(sample_rate);
	wavspec.format = AUDIO_S16SYS;
	wavspec.channels = audio_file_channels(audio_file);
	wavspec.samples = 4096;
	wavspec.callback = sdl_fill_audio;
	wavspec.userdata = audio_file;

	if (SDL_OpenAudio(&wavspec, NULL) < 0) {
	    fprintf(stderr, "Couldn't initialize SDL audio: %s.\n", SDL_GetError());
	    exit(1);
	}
    }
#endif

    /* Start FFT calculator */
    calc_columns(0, disp_width - 1);

    /* Start screen-updating and scrolling timer */
#if ECORE_TIMER
    /* The timer callback just generates an event, which is processed in
     * the main ecore event loop to do the scrolling in the main loop
     */
    scroll_event = ecore_event_type_new();
    ecore_event_handler_add(scroll_event, scroll_cb, NULL);
    timer = ecore_timer_add(step, timer_cb, (void *)em);
#elif SDL_TIMER
    timer = SDL_AddTimer((Uint32)lrint(step * 1000), timer_cb, (void *)NULL);
#endif
    if (timer == NULL) {
	fprintf(stderr, "Couldn't initialize scrolling timer.\n");
	exit(1);
    }

#if ECORE_MAIN
    /* Start main event loop */
    ecore_main_loop_begin();
#elif SDL_MAIN
    {
	SDL_Event event;
	enum key key;

	while (SDL_WaitEvent(&event)) switch (event.type) {

	case SDL_QUIT:
	    exit(0);

	case SDL_KEYDOWN:
	    Shift   = !!(event.key.keysym.mod & KMOD_SHIFT);
	    Control = !!(event.key.keysym.mod & KMOD_CTRL);
	    key     = KEY_NONE;

	    switch (event.key.keysym.sym) {
	    case SDLK_q:
	    case SDLK_c:if (Control) key = KEY_QUIT;	break;
	    case SDLK_SPACE:	     key = KEY_SPACE;	break;
	    case SDLK_LEFT:	     key = KEY_LEFT;	break;
	    case SDLK_RIGHT:	     key = KEY_RIGHT;	break;
	    case SDLK_UP:	     key = KEY_UP;	break;
	    case SDLK_DOWN:	     key = KEY_DOWN;	break;
	    case SDLK_x:	     key = KEY_X;	break;
	    case SDLK_y:	     key = KEY_Y;	break;
	    case SDLK_PLUS:	     key = KEY_PLUS;	break;
	    case SDLK_MINUS:	     key = KEY_MINUS;	break;
	    case SDLK_ASTERISK:	     key = KEY_STAR;	break;
	    case SDLK_SLASH:	     key = KEY_SLASH;	break;
	    case SDLK_p:	     key = KEY_P;	break;
	    case SDLK_s:	     key = KEY_S;	break;
	    case SDLK_g:	     key = KEY_G;	break;
	    case SDLK_LEFTBRACKET:   key = KEY_BAR_START;break;
	    case SDLK_RIGHTBRACKET:  key = KEY_BAR_END; break;
	    }
	    if (key != KEY_NONE) do_key(key);
	    break;

	case SDL_MOUSEBUTTONDOWN:
	    {
		double when = (event.button.x - disp_offset) * step;
		/* To detect Shift and Control states, it looks like we have to
		 * examine the keys ourselves */
		bool shift, ctrl;
		Uint8 *keystate = SDL_GetKeyState(NULL);

		shift = keystate[SDLK_LSHIFT] || keystate[SDLK_RSHIFT];
		ctrl = keystate[SDLK_LCTRL] || keystate[SDLK_RCTRL];

		if (ctrl) switch (event.button.button) {
		case SDL_BUTTON_LEFT:
		    set_col_overlay_left(when); break;
		case SDL_BUTTON_RIGHT:
		    set_col_overlay_right(when); break;
		}
	    }
	    break;

	case SDL_VIDEORESIZE:
	    /* One day */
	    break;

	case SDL_USEREVENT:
	    /* Column result from a calculation thread */
	    {
		result_t *result = (result_t *) event.user.data1;
		calc_notify(result);
	    }

	    break;

	default:
	    break;
	}
    }
#endif

quit:
    /* Tidy up and quit */
#if EVAS_VIDEO
#if 0
    /* Either of these makes it dump core */
    ecore_evas_free(ee);
    ecore_evas_shutdown();
#endif
#endif

#if SDL_AUDIO || SDL_TIMER || SDL_VIDEO || SDL_MAIN
    SDL_Quit();
#endif

    return 0;
}

#if SDL_AUDIO
/*
 * SDL audio callback function to fill the buffer at "stream" with
 * "len" bytes of audio data.
 */
static void
sdl_fill_audio(void *userdata, Uint8 *stream, int len)
{
	audio_file_t *audiofile = (audio_file_t *)userdata;
	int nchannels = audio_file_channels(audiofile);
	int frames_to_read = len / (sizeof(short) * nchannels);
	int frames_read;	/* How many were read from the file */

	if ((frames_read = read_audio_file(audiofile, stream,
			    af_signed, nchannels,
			    sdl_start, frames_to_read)) <= 0) {
	    /* End of file or read error. Treat as end of file */
	    SDL_PauseAudio(1);
	}
	sdl_start += frames_read;

	/* SDL has no "playback finished" callback, so spot it here */
	if (sdl_start >= audio_file_length_in_frames(audiofile)) {
	    stop_playing(NULL);
	}
}
#endif

/*
 * Schedule the FFT thread to calculate the results for these display columns
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

#if ECORE_MAIN
    thread = ecore_thread_feedback_run(
	calc_heavy, calc_notify, calc_end, calc_cancel, calc, EINA_FALSE);
    if (thread == NULL) {
	fprintf(stderr, "Can't start FFT-calculating thread.\n");
	exit(1);
    }
#elif SDL_MAIN
    {
	pthread_attr_t attr;

	if (pthread_attr_init(&attr) != 0 ||
	    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
	    fprintf(stderr, "Failed to create/set thread attributes.\n");
	    return;
	}
	if (pthread_create(&(calc->thread), &attr, calc_heavy, calc) != 0) {
	    fprintf(stderr, "Cannot create calculation thread: %s\n",
		    strerror(errno));
	    return;
	}
	pthread_attr_destroy(&attr);
    }
#endif
}

/*
 *	GUI callbacks
 */

#if ECORE_MAIN

/* Quit on window close or Control-Q */
static void
quitGUI(Ecore_Evas *ee EINA_UNUSED)
{
    //calc_stop();
    ecore_main_loop_quit();
}

/*
 * Keypress events
 *
 * Other interesting key names are:
 *	"Prior"		PgUp
 *	"Next"		PgDn
 *	"XF86AudioPlay"	Media button >
 *	"XF86AudioStop"	Media button []
 *	"XF86AudioPrev"	Media button <<
 *	"XF86AudioNext"	Media button >>
 *
 * The SDL equivalent of this is in SDL's main lood at the end of main().
 */

static void
keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Key_Down *ev = einfo;
    Evas_Object *em = data;	/* The Emotion object */
    const Evas_Modifier *mods = evas_key_modifier_get(evas);
    enum key key = KEY_NONE;

    Shift = evas_key_modifier_is_set(mods, "Shift");
    Control = evas_key_modifier_is_set(mods, "Control");

    if (Control && (!strcmp(ev->key, "q") || !strcmp(ev->key, "c")))
	key = KEY_QUIT;
    else if (!strcmp(ev->key, "space"))
	key = KEY_SPACE;
    else if (!strcmp(ev->key, "Left") || !strcmp(ev->key, "KP_Left"))
	key = KEY_LEFT;
    else if (!strcmp(ev->key, "Right") || !strcmp(ev->key, "KP_Right"))
	key = KEY_RIGHT;
    else if (!strcmp(ev->key, "Up") || !strcmp(ev->key, "KP_Up"))
	key = KEY_UP;
    else if (!strcmp(ev->key, "Down") || !strcmp(ev->key, "KP_Down"))
	key = KEY_DOWN;
    else if (!strcmp(ev->key, "x") || !strcmp(ev->key, "X"))
	key = KEY_X;	/* ...hoping that Shift is set properly */
    else if (!strcmp(ev->key, "y") || !strcmp(ev->key, "Y"))
	key = KEY_Y;
    else if (!strcmp(ev->key, "plus") || !strcmp(ev->key, "KP_Add"))
	key = KEY_PLUS;
    else if (!strcmp(ev->key, "minus") || !strcmp(ev->key, "KP_Subtract"))
	key = KEY_MINUS;
    else if (!strcmp(ev->key, "asterisk") || !strcmp(ev->key, "KP_Multiply"))
	key = KEY_STAR;
    else if (!strcmp(ev->key, "slash") || !strcmp(ev->key, "KP_Divide"))
	key = KEY_SLASH;
    else if (!strcmp(ev->key, "p"))
	key = KEY_P;
    else if (!strcmp(ev->key, "s"))
	key = KEY_S;
    else if (!strcmp(ev->key, "g"))
	key = KEY_G;
    else if (!strcmp(ev->key, "bracketleft"))
	key = KEY_BAR_START;
    else if (!strcmp(ev->key, "bracketright"))
	key = KEY_BAR_END;
/*
    else
	fprintf(stderr, "Key \"%s\" was pressed.\n", ev->key);
 */

    do_key(key);
}

static void
mouseDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Object *em = data;	/* The Emotion object */

    /* Evas_Point *output = &(ev->output); /* In world coordinates */
    Evas_Coord_Point *where = &(ev->canvas);
    double when = (where->x - disp_offset) * step;
    Evas_Modifier *modifiers = ev->modifiers;
    bool shift = evas_key_modifier_is_set(modifiers, "Shift");
    bool control = evas_key_modifier_is_set(modifiers, "Control");

    /* Bare left and right click: position bar lines */
    if (control) {
	switch (ev->button) {
	case 1:
	    set_col_overlay_left(when); break;
	case 3:
	    set_col_overlay_right(when); break;
	}
    }
}
#endif

/*
 * Process a keystroke.  Also inspects the variables Control and Shift.
 */
static void
do_key(enum key key)
{
    switch (key) {
    case KEY_NONE:	/* They pressed something else */
	break;
    case KEY_QUIT:	/* Quit */
#if ECORE_MAIN
	ecore_main_loop_quit();
#elif SDL_MAIN
	exit(0);	/* atexit() calls SDL_Quit() */
#endif
	break;
    case KEY_SPACE:	/* Play/Pause/Replay */
	switch (playing) {
	case PLAYING:
	    pause_playing();
	    break;

	case STOPPED:
	    disp_time = 0.0;
	    repaint_display();
	    calc_columns(disp_offset, disp_width);
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
	if (key == KEY_P) piano_lines = !piano_lines;
	if (key == KEY_S) if (staff_lines = !staff_lines) guitar_lines = FALSE;
	if (key == KEY_G) if (guitar_lines = !guitar_lines) staff_lines = FALSE;
	make_row_overlay();
	repaint_display();
	break;

    /* Set left or right bar line position to current play position */
    case KEY_BAR_START:
	set_col_overlay_left(disp_time);
	break;
    case KEY_BAR_END:
	set_col_overlay_right(disp_time);
	break;

    default:
	fprintf(stderr, "Bogus KEY_ number %d\n", key);
    }
}

/* Audio-playing functions */

static void
pause_playing()
{
#if EMOTION_AUDIO
    emotion_object_play_set(em, EINA_FALSE);
#endif
#if SDL_AUDIO
    SDL_PauseAudio(1);
#endif
    playing = PAUSED;
}

static void
start_playing()
{
#if EMOTION_AUDIO
    emotion_object_position_set(em, disp_time + pending_seek);
    emotion_object_play_set(em, EINA_TRUE);
#endif
#if SDL_AUDIO
    sdl_start = lrint((disp_time + pending_seek) * sample_rate);
    SDL_PauseAudio(0);
#endif
    playing = PLAYING;
}

/* Stop playing because it has arrived at the end of the piece */
static void
stop_playing()
{
#if EMOTION_AUDIO
    emotion_object_play_set(em, EINA_FALSE);
#endif
#if SDL_AUDIO
    /* Let SDL play last buffer of piece and pause on its own */
    /* SDL_PauseAudio(1); */
#endif

    /* These settings indicate that the player has stopped at end of track */
    playing = STOPPED;
    disp_time = floor(audio_length / step + DELTA) * step;

    if (exit_when_played) {
#if ECORE_MAIN
	ecore_main_loop_quit();
#elif SDL_MAIN
	SDL_Quit();
#endif
    }
}

static void
continue_playing()
{
#if EMOTION_AUDIO
    /* Resynchronise the playing position to the display,
     * as emotion stops playing immediately but seems to throw away
     * the unplayed part of the currently-playing audio buffer.
     */
    emotion_object_position_set(em, disp_time);
    emotion_object_play_set(em, EINA_TRUE);
#endif
#if SDL_AUDIO
    sdl_start = lrint(disp_time * sample_rate);
    SDL_PauseAudio(0);
#endif
    playing = PLAYING;
}

/*
 * Jump forwards or backwards in time, scrolling the display accordingly.
 * Here we just set pending_seek; the actual scrolling is done in timer_cb().
 */
static void
time_pan_by(double by)
{
    double playing_time;

    pending_seek += by;
    playing_time = disp_time + pending_seek;
    if (playing_time < 0.0) playing_time = 0.0;
    if (playing_time > audio_length) {
	playing_time = audio_length;
	pending_seek = playing_time - disp_time;
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
#if EMOTION_AUDIO
    emotion_object_position_set(em, playing_time);
#endif
#if SDL_AUDIO
    sdl_start = lrint(playing_time * sample_rate);
#endif

    /* If moving left after it has come to the end and stopped,
     * we want it to play again. */
    if (by < 0.0 && playing == STOPPED && playing_time <= audio_length) {
	start_playing();
    }
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
#if ECORE_TIMER
    (void) ecore_timer_del(timer);
    timer = ecore_timer_add(step, timer_cb, (void *)em);
    if (timer == NULL) {
#elif SDL_TIMER
    if (!SDL_RemoveTimer(timer) ||
	(timer = SDL_AddTimer((Uint32)lrint(step * 1000), timer_cb, NULL)) == NULL) {
#endif
	fprintf(stderr, "Couldn't change rate of scrolling timer.\n");
	exit(1);
    }

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
 *	Audio callbacks
 */

#if EMOTION_AUDIO
/*
 * Callback is called when the player gets to the end of the piece.
 *
 * The "playback_started" event is useless because in emotion 0.28 it is
 * delivered when playback of audio finishes (!)
 * An alternative would be the "decode_stop" callback but "playback_finished"
 * is delivered first.
 */
static void
playback_finished_cb(void *data, Evas_Object *obj, void *ev)
{
    stop_playing(em);
}
#endif

/*
 * The periodic timer callback that, when playing, schedules scrolling of
 * the display by one pixel.
 * When paused, the timer continues to run to update the display in response to
 * seek commands.
 */

#if ECORE_TIMER

static Eina_Bool
timer_cb(void *data)
{
    /* Generate a user-defined event which will be processed in the main loop */
    ecore_event_add(scroll_event, NULL, NULL, NULL);
    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
scroll_cb(void *data, int type, void *event)
{
    do_scroll();
    return ECORE_CALLBACK_DONE;
}

#elif SDL_TIMER

static Uint32
timer_cb(Uint32 interval, void *data)
{
    do_scroll();

    /* Should use SDL_Ticks() to make it keep in sync with the audio */
    return(interval);
}

#endif

/*
 * Really scroll the screen according to pending_seek
 */
static void
do_scroll()
{
    double new_disp_time;	/* Where we reposition to */
    int scroll_by;		/* How many pixels to scroll by.
				 * +ve = move forward in time, move display left
				 * +ve = move back in time, move display right
				 */
    /*
     * emotion's position reporting is unreliable and grainy.
     * We get smoother scrolling especially after repositioning
     * just by beating time.
     */
    new_disp_time = disp_time + pending_seek; pending_seek = 0.0;
    if (playing == PLAYING) new_disp_time += step;

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

    if (scroll_by == 0) return;

    /*
     * Scroll the display sideways by the correct number of pixels
     * and start a calc thread for the newly-revealed region.
     *
     * (4 * scroll_by) is the number of bytes by which we scroll.
     * The right-hand columns will fill with garbage or the start of
     * the next pixel row, and the final "- (4*scroll_by)" is so as
     * not to scroll garbage from past the end of the frame buffer.
     */

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
	int y;
#if EVAS_VIDEO
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

/* Tell the video subsystem to update the whole display from the pixel data */
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

/* The function called to start a calculation */
#if ECORE_MAIN
static void
calc_heavy(void *data, Ecore_Thread *thread)
#elif SDL_MAIN
static void *
calc_heavy(void *data)
#endif
{
    calc_t *c = (calc_t *)data;
#if ECORE_MAIN
    c->thread = thread;
#elif SDL_MAIN
    /* SDL stores the thread ID in the structure when it creates the thread */
#endif
    calc(c, calc_result);

#if SDL_MAIN
    return(NULL);
#endif

}

/* The callback called by calculation threads to report a result */
static void
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
	event.user.data1 = result;
	if (SDL_PushEvent(&event) != 0) {
	    /* The Event queue is full. let it empty and try again. */
	    sleep(1);
	    if (SDL_PushEvent(&event) != 0) {
		fprintf(stderr, "Couldn't post a result event\n");
		return;
	    }
	}
    }
#endif
}

#if ECORE_MAIN
/* Functions called in the main loop when a thread
 * calc_heavy 	The function that should run in another thread.
 * calc_notify 	The function that will receive the data send by func_heavy in the main loop.
 * calc_end 	The function that will be called in the main loop if the thread terminate correctly.
 * calc_cancel 	The function that will be called in the main loop if the thread is cancelled.
 */
static void
calc_end(void *data, Ecore_Thread *thread)
{
    free(data);
}

static void
calc_cancel(void *data, Ecore_Thread *thread)
{
    free(data);
}

static void
calc_stop(void)
{
    // (void) ecore_thread_cancel(calc->thread);
}
#endif

static void
#if ECORE_MAIN
calc_notify(void *data, Ecore_Thread *thread, void *msg_data)
#elif SDL_MAIN
calc_notify(result_t *result)
#endif
{
#if ECORE_MAIN
    calc_t   *calc   = (calc_t *)data;
    result_t *result = (result_t *)msg_data;
#endif
    int pos_x;	/* Where would this column appear in the displayed region? */

    /* The Evas image that we need to write to */

    /* What screen coordinate does this result correspond to? */
    pos_x = lrint(disp_offset + (result->t - disp_time) * ppsec);

    remember_result(result);

    /* Update the display if the column is in the displayed region
     * and isn't at the green line's position
     */
    if (pos_x >= 0 && pos_x < disp_width &&
	pos_x != disp_offset) {
	paint_column(pos_x, result);
	update_column(pos_x);
    }

    /* To avoid an embarassing pause at the start of the graphics, we wait
     * until the FFT delivers its first result before starting the player.
     */
    if (autoplay && playing != PLAYING) {
	start_playing();
    }
}

/*
 * Result cache, inside main.c because it wants to access disp_time
 * to know when to throw away old results.
 */

/* "result" was obtained from malloc(); it is up to us to free it. */
static void
remember_result(result_t *result)
{
    /* Drop any stored results that are before the displayed area */
    while (results != NULL && results->t < disp_time - disp_offset * step - DELTA) {
	result_t *r = results;
	results = results->next;
	destroy_result(r);
    }
    if (results == NULL) last_result = NULL;

    result->next = NULL;

    if (last_result == NULL) {
	results = last_result = result;
    } else {
        /* If after the last one, add at tail of list */
	if (result->t > last_result->t + DELTA) {
	    last_result->next = result;
	    last_result = result;
	} else {
	    /* If it's before the first one, tack it onto head of list */
	    if (result->t < results->t - DELTA) {
		result->next = results;
		results = result;
	    } else {
		/* Otherwise find which element to place it after */
		result_t *r;	/* The result after which we will place it */
		for (r=results;
		     r && r->next && r->next->t <= result->t + DELTA;
		     r = r->next) {
		    if (r->next->t <= result->t + DELTA &&
			r->next->t >= result->t - DELTA) {
			/* Same time: forget it */
			destroy_result(result);
			r = NULL; break;
		    }
		}
		if (r) {
		    result->next = r->next;
		    r->next = result;
		    if (last_result == r) last_result = result;
	        }
	    }
	}
    }
}

/* Return the result for time t at the current speclen
 * or NULL if it hasn't been calculated yet */
static result_t *
recall_result(double t)
{
    result_t *p;

    /* If it's later than the last cached result, we don't have it.
     * This saves uselessly scanning the whole list of results.
     */
    if (last_result == NULL || t > last_result->t + DELTA)
	return(NULL);

    for (p=results; p != NULL; p=p->next) {
	/* If the time is the same, this is the result we want */
	if (p->t >= t - DELTA && p->t <= t + DELTA) {
	    break;
	}
	/* If the stored time is greater, it isn't there. */
	if (p->t > t + DELTA) {
	    p = NULL;
	    break;
	}
    }
    return(p);	/* NULL if not found */
}

/* Free the memory associated with a result structure */
static void
destroy_result(result_t *r)
{
    free(r->spec);
    free(r->mag);
    free(r);
}

/*
 * Stuff to draw overlays on the graphic
 *
 * - horizontal lines showing the frequencies of piano keys and/or
 *   of the conventional score notation pentagram lines.
 *   RSN: Guitar strings!
 * - vertical lines to mark the bars and beats, user-adjustable
 *
 * == Row overlay ==
 *
 * The row overlay is implemented by having an array with an element for each
 * pixel of a screen column, with each element saying whether there's an overlay
 * color at that height: 0 means no, 0xFFRRGGBB says of which colour if so.
 *
 * When the vertical axis is panned or zoomed, or the vertical window size
 * changes, the row overlay matrix must be recalculated.
 *
 * == Column overlay ==
 *
 * The column overlay shows (will show!) draggable bar lines three pixels wide
 * with intermediate beat markers 1 pixel wide. The column overlay takes
 * priority over the row overlay, so that "bar lines" are maintained whole.
 *
 * The column overlay is created with mouse clicks, say:
 * Left button to mark the start of a bar, right to mark the end of a bar.
 * A three-pixel wide vertical bar appears at that point, when both are given
 * the bar lines are repeated left and right at that separation.
 *
 * Numeric keys then set the number of beats per bar, shown as 1-pixel-wide
 * vertical bars.
 * Both bar and beat lines are extended left and right of the marked points.
 */

/* The array of overlay colours for every pixel column,
 * indexed from y=0 at the bottom to disp_height-1
 */
static unsigned int *row_overlay = NULL;

/* and we remember what parameters we calculated it for so as to recalculate it
 * automatically if anything changes.
 */
static double row_overlay_min_freq;
static double row_overlay_max_freq;
static int    row_overlay_len;

/*
 * Calculate the overlays
 */

static void
make_row_overlay()
{
    int note;	/* Of 88-note piano: 0 = Bottom A, 87 = top C */
#define NOTE_A440	48  /* A above middle C */
    static double half_a_semitone = 0.0;
    int len = disp_height;

    if (half_a_semitone == 0.0)
	half_a_semitone = pow(2.0, 1/24.0);

    /* Check allocation of overlay array and zero it */
    if (row_overlay == NULL ) {
        row_overlay = malloc(len *sizeof(unsigned int));
      if (row_overlay == NULL )
          /* Continue with no overlay */
          return;
      }
      row_overlay_len = len;

    /* Check for resize */
    if (row_overlay_len != len) {
      row_overlay = realloc(row_overlay, len *sizeof(unsigned int));
      if (row_overlay == NULL )
          /* Continue with no overlay */
          return;
      row_overlay_len = len;
    }
    memset(row_overlay, 0, len * sizeof(unsigned int));

    if (piano_lines) {
	/* Run up the piano keyboard blatting the pixels they hit */
	for (note = 0; note < 88; note++) {
	    /* Colour of notes in octave,  starting from A */
	    static bool color[12] = { 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1 };

#define note_to_freq(note) (440.0 * pow(2.0, (1/12.0) * (note - NOTE_A440)))
#define freq_to_magindex(freq)	lrint((log(freq) - log(min_freq)) /	\
				(log(max_freq) - log(min_freq)) *	\
				(disp_height - 1))

	    double freq = note_to_freq(note);
	    int magindex = freq_to_magindex(freq);

	    /* If in screen range, write it to the overlay */
	    if (magindex >= 0 && magindex < len)
		row_overlay[magindex] = (color[note % 12] == 0)
				    ? 0xFFFFFFFF	/* 0=White */
				    : 0xFF000000;	/* 1=Black */
	}
    }

    if (staff_lines) {
	/* Which note numbers do the staff lines fall on? */
	static int notes[] = {
	    22, 26, 29, 32, 36,	/* G2 B2 D3 F3 A3 */
	    43, 46, 50, 53, 56	/* E4 G4 B4 D5 F5 */
	};
	int i;

	for (i=0; i < sizeof(notes)/sizeof(notes[0]); i++) {
	    double freq = note_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);

	    /* Staff lines are 3 pixels wide */
	    if (magindex >= 0 && magindex < len)
		row_overlay[magindex] = 0xFFFFFFFF;
	    if (magindex-1 >= 0 && magindex-1 < len)
		row_overlay[magindex-1] = 0xFFFFFFFF;
	    if (magindex+1 >= 0 && magindex+1 < len)
		row_overlay[magindex+1] = 0xFFFFFFFF;
        }
    }

    if (guitar_lines) {
	/* Which note numbers do the guitar strings fall on? */
	static int notes[] = {
	    19, 24, 29, 34, 38, 43  /* Classical guitar: E2 A2 D3 G3 B3 E4 */
	};
	int i;

	for (i=0; i < sizeof(notes)/sizeof(notes[0]); i++) {
	    double freq = note_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);

	    /* Guitar lines are also 3 pixels wide */
	    if (magindex >= 0 && magindex < len)
		row_overlay[magindex] = 0xFFFFFFFF;
	    if (magindex-1 >= 0 && magindex-1 < len)
		row_overlay[magindex-1] = 0xFFFFFFFF;
	    if (magindex+1 >= 0 && magindex+1 < len)
		row_overlay[magindex+1] = 0xFFFFFFFF;
        }
    }
}

/* What colour overlays this pixel row?
 * 0x00000000 = Nothing
 * 0xFFrrggbb = this colour
 */
static unsigned int
get_row_overlay(int y)
{
    if (row_overlay == NULL) return 0;

    /* If anything moved, recalculate the overlay.
     *
     * Changes to piano_lines or score_lines
     * will call make_row_overlay explicitly; these
     * others can change asynchronously.
     */
    if (row_overlay_min_freq != min_freq ||
	row_overlay_max_freq != max_freq ||
	row_overlay_len != disp_height - 1)
    {
	make_row_overlay();
	if (row_overlay == NULL) return 0;
	row_overlay_min_freq = min_freq;
	row_overlay_max_freq = max_freq;
	row_overlay_len = disp_height - 1;
    }

    return row_overlay[y];
}

/*
 * Column overlays marking bar lines and beats
 *
 * The column overlays depend on the clicked start and end of a bar,
 * measured in pixels, not time, for convenience.
 * If a beat line doesn't fall exactly on a pixel's timestamp, we round
 * it to the nearest pixel.
 */

/* Markers for start and end of bar in pixels from the start of the piece.
 *
 * Maybe: with no beats, 1-pixel-wide bar line.
 * With beats, 3 pixels wide.
 */
#define UNDEFINED (-1.0)
static double col_overlay_left_time = UNDEFINED;
static double col_overlay_right_time = UNDEFINED;
static unsigned int beats_per_bar = 0;	/* 0 = No beat lines, only bar lines */

/* Set start and end of marked bar. */
static void
set_col_overlay_left(double when)
{
    /* Setting left to the right of right cancels right */
    if (col_overlay_right_time != UNDEFINED && when > col_overlay_right_time)
	col_overlay_right_time = UNDEFINED;

    col_overlay_left_time = when;
    repaint_display();
}

static void
set_col_overlay_right(double when)
{
    unsigned int x = lrint(when / step);

    /* Setting right to the left of left cancels left */
    if (col_overlay_left_time != UNDEFINED && when < col_overlay_left_time)
	col_overlay_left_time = UNDEFINED;

    col_overlay_right_time = when;
    repaint_display();
}

/*
 * What colour overlays this screen column?
 *
 * 0x00000000 = Nothing
 * 0xFFrrggbb = this colour
 */
static unsigned int
get_col_overlay(int x)
{
    /* The bar position converted to a pixel index into the whole piece */
    unsigned int col_overlay_left_ticks = lrint(col_overlay_left_time / step);
    unsigned int col_overlay_right_ticks = lrint(col_overlay_right_time / step);

    /* If neither of the bar positions is defined, do nothing */
    if (col_overlay_left_time == UNDEFINED &&
	col_overlay_right_time == UNDEFINED) return 0;

    /* Convert x to column index in whole piece */
    x += lrint(disp_time / step) - disp_offset;

    /* If only one of the bar positions is defined, paint it.
     * Idem if they've defined both bar lines at the same pixel.
     */
    if (col_overlay_left_time == UNDEFINED ||
	col_overlay_right_time == UNDEFINED ||
	col_overlay_left_ticks == col_overlay_right_ticks) {

	if (x == col_overlay_left_ticks || x == col_overlay_right_ticks)
	    return 0xFFFFFFFF;
	else
	    return 0;
    }

    /* This should never happen */
    if (col_overlay_left_ticks > col_overlay_right_ticks) {
	return 0;
    }

    /* Both bar positions are defined. See if this column falls on one. */
    {
	/* How long is the bar in pixels? */
        unsigned int bar_width = col_overlay_right_ticks - col_overlay_left_ticks;
	if (x % bar_width == col_overlay_left_ticks % bar_width)
	    return 0xFFFFFFFF;	/* White */
	else
	    return 0;
    }
}
