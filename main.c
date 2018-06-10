/*
 * Program: spettro
 *	Play an audio file displaying a scrolling log-frequency spectrogram.
 *
 * File: emotion.c
 *	Main routine implemented using Enlightenment's "emotion" interface.
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
 * It computes the spectrogram from start to finish, displaying the results
 * pixel column immediately if it falls within the displayed region and
 * displaying already-computed columns in blank regions that are revealed
 * as the view moves in the audio file.
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
 * The left and right arrow keys jump back or forwards by 1 second
 * or 10 if shift is held.
 *
 * Variants:
 * -p	Play the audio file and start scrolling the display immediately
 * -e	Exit when the audio file has finished playing
 * -w n	Open the window n pixels wide (default: 640)
 * -h n	Open the window n pixels high (default: 480)
 *
 * If you resize the window the displayed image is zoomed.
 *
 * If you hit Control-Q or poke the [X] icon in the window's titlebar, it quits.
 *
 * it runs in two threads:
 * - the main thread handles GUI events, starts/stops the audio player,
 *   tells the calc thread what to calculate, receives results, and
 *   displays them.
 * - The calc thread performs FFTs and reports back when they're done.
 *
 * == WIBNIs ==
 *
 * One day, there will be a frequency scale in hertz on the left of the
 * spectrogram and on the right a frequency scale in musical notes with
 * optional overlays of the ten ceonventional-notation staff lines and/or
 * the notes of the piano as black and white one-pixel-high horizontal lines.
 * Along the top or bottom there may be a time scale in seconds.
 *
 * Another day, you'll be able to overlay a time grid, anchored to the sound,
 * with vertical lines marking beats in 50% green and first beats of bar in
 * 100% green (or 50% red?).
 * When the bar lines are displayed, the user can drag individual beat lines;
 * the first time they do this, the rest of the beat lines pan. From the second
 * time onward, moving a different bar line stretches the beats between the
 * beat line they're dragging and the last beat line that they dropped.
 *
 * We'll need to give some way to change the number of beats per bar, maybe
 * mark the beat lines manually, by default repeating the l.
 *
 * It would be nice to be able to write on the spectrogram with a one-pixel
 * green pencil, which suggests storing the pixel data instead of the amplitude.
 * Then, if you press s, it saves the spectrogram as audiofilename.png?
 * Then, when you reload, it imports from audiofilename.png
 * all pixels of the pencil colour as an overlay on the spectral data.
 * That would allow us to recompute the spectrogram with different parameters
 * but preserving the annotations.
 *
 *	Martin Guy <martinwguy@gmail.com>, Dec 2016 - May 2017.
 */

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Evas.h>
#if EMOTION_AUDIO
#include <Emotion.h>
#endif

#include <stdlib.h>
#include <math.h>	/* for lrint() */

#if SDL_AUDIO
# include <SDL/SDL.h>
static unsigned sdl_start = 0;	/* At what offset in the audio file, in frames,
				 * will we next read samples to play? */
#endif

#include "spettro.h"
#include "audiofile.h"
#include "calc.h"
#include "interpolate.h"
#include "colormap.h"
#include "speclen.h"

/*
 * Function prototypes
 */

/* Helper functions */
static void	calc_columns(int from, int to, Evas_Object *em);
static void	remember_result(result_t *result);
static result_t *recall_result(double t);
static void	destroy_result(result_t *r);
static void	repaint_display(Evas_Object *em);
static bool	repaint_column(int column, Evas_Object *em);
static void	paint_column(int column, result_t *result);
static void	green_line(void);

/* GUI callbacks */
static void keyDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void quitGUI(Ecore_Evas *ee);

/* Audio playing functions */
static void pause_playing(Evas_Object *em);
static void start_playing(Evas_Object *em);
static void stop_playing(Evas_Object *em);
static void continue_playing(Evas_Object *em);
static void time_pan_by(Evas_Object *em, double by);	/* Left/Right */
static void time_zoom_by(Evas_Object *em, double by);	/* x/X */
static void freq_pan_by(Evas_Object *em, double by);	/* Up/Down */
static void freq_zoom_by(Evas_Object *em, double by);	/* y/Y */
static void change_dyn_range(Evas_Object *em, double by);/* * and / */

static Ecore_Timer *timer = NULL;
static Eina_Bool timer_cb(void *data);

/* Audio callback functions */
#if EMOTION_AUDIO
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);
#endif
#if SDL_AUDIO
static void sdl_fill_audio(void *userdata, Uint8 *stream, int len);
#endif

/* FFT calculating thread */
static void calc_heavy(void *data, Ecore_Thread *thread);
static void calc_notify(void *data, Ecore_Thread *thread, void *msg_data);
static void calc_end(void *data, Ecore_Thread *thread);
static void calc_cancel(void *data, Ecore_Thread *thread);
static void calc_stop(void);

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
static bool log_freq	= TRUE;		/* Use a logarithmic frequency axis? */
static bool gray	= FALSE;	/* Display in shades of gray? */

/* The colour for uncalculated areas: Alpha 255, RGB gray */
#define background 0xFF808080

/* Internal data used in notify callback to write on the image buffer */
static Evas_Object *image;
static unsigned char *imagedata = NULL;
static int imagestride;

/* What the audio subsystem is doing:
 * STOPPED means it has reached the end of the piece and stopped automatically
 * PLAYING means it should be playing audio,
 * PAUSED  means we've paused it
 */
static enum { STOPPED, PLAYING, PAUSED } playing = PAUSED;

static audio_file_t *audio_file;
static double	audio_length = 0.0;	/* Length of the audio in seconds */
static double	sample_rate;		/* SR of the audio in Hertz */

/* option flags */
static bool autoplay = FALSE;	/* -p  Start playing the file right away */
static bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
static int  max_threads = 0;	/* 0 means use default (the number of CPUs) */

/* State variables (hacks) */

/* When they press arrow left or right to seek, we just increment pending_seek;
 * the screen updating is then done in the next timer callback.
 */
static double pending_seek = 0.0;

/*
 * Internal data used to remember the FFT result for each pixel value.
 * For now, it's just an array indexing them. When we can zoom this will
 * need redoing.
 *
 * We keep the results in time order from oldest to newest.
 */
static result_t *results = NULL; /* Linked list of result structures */
static result_t *last_result = NULL; /* Last element in the linked list */

int
main(int argc, char **argv)
{
    Ecore_Evas *ee;
    Evas *canvas;
    Evas_Object *em;
    char *filename;

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
	    if ((disp_width = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-w what?\n");
		exit(1);
	    }
	    break;
	case 'h':
	    argv++; argc--;	 /* Advance to numeric argument */
	    if ((disp_height = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-h what?\n");
		exit(1);
	    }
	    break;
	case 'j':
	    argv++; argc--;	 /* Advance to numeric argument */
	    if ((max_threads = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-j what?\n");
		exit(1);
	    }
	    break;
	case 'v':
	    printf("Version: %s\n", VERSION);
	    exit(0);
	default:
	    fprintf(stderr,
"Usage: spettro [-p] [-e] [-h n] [-w n] [file.wav]\n\
-p:\tPlay the file right away\n\
-e:\tExit when the audio file has played\n\
-h n:\tSet spectrogram display height to n pixels\n\
-w n:\tSet spectrogram display width to n pixels\n\
-j n:\tSet maximum number of threads to use (default: the number of CPUs)\n\
-v:\tPrint the version of spettro that you're using\n\
The default file is audio.wav\n\
Keyboard commands:\n\
Ctrl-Q     Quit\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by one second (10 seconds if Shift is held)\n\
Up/Down    Pan up/down the frequency axis by a semitone (an octave if Shift)\n\
X/x        Zoom in/out on the time axis by a factor of 2\n\
Y/y        Zoom in/out on the frequency axis by a factor of 2\n\
Ctrl-+/-   Zoom in/out on both axes\n\
Star/Slash Change the dynamic range to brighten/darken the darker areas\n\
Environment variables:\n\
PPSEC      Pixel columns per second, default %g\n\
FFTFREQ    FFT audio window is 1/this, default 1/%g of a second\n\
DYN_RANGE  Dynamic range of amplitude values in decibels, default=%g\n\
", ppsec, fftfreq, -min_db);
	    exit(1);
	}
    }

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
    }

    /* Set variables with derived values */

    disp_offset = disp_width / 2;
    step = 1 / ppsec;

    /* Set default values for unset parameters */

    filename = (argc > 0) ? argv[0] : "audio.wav";

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
    /* Clear the image buffer to the background colour */
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

    /* Initialize the audio subsystem */

#if EMOTION_AUDIO
    em = emotion_object_add(canvas);
#else
    em = evas_object_smart_add(canvas, NULL);
#endif
    if (!em) {
#if EMOTION_AUDIO
	fputs("Couldn't initialize audio.\n", stderr);
#else
	fputs("Couldn't initialize graphics.\n", stderr);
#endif
	exit(1);
    }

#if SDL_AUDIO
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
	fprintf(stderr, "Couldn't initialize SDL audio: %s.\n", SDL_GetError());
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
#endif
    evas_object_show(em);

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

    /* Set GUI callbacks */
    evas_object_event_callback_add(image, EVAS_CALLBACK_KEY_DOWN, keyDown, em);

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
    calc_columns(0, disp_width - 1, em);

    /* Start screen-updating and scrolling timer */
    timer = ecore_timer_add(step, timer_cb, (void *)em);

    /* Start main event loop */
    ecore_main_loop_begin();

quit:
    /* Tidy up and quit */
#if 0
    /* Either of these makes it dump core */
    ecore_evas_free(ee);
    ecore_evas_shutdown();
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

static void
calc_columns(int from, int to, Evas_Object *em)
{
    calc_t *calc = malloc(sizeof(calc_t));
    Ecore_Thread *thread;

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
    calc->data   = em;	/* Needed to start player when calc is ready */

    thread = ecore_thread_feedback_run(
	calc_heavy, calc_notify, calc_end, calc_cancel, calc, EINA_FALSE);
    if (thread == NULL) {
	fprintf(stderr, "Can't start FFT-calculating thread.\n");
	exit(1);
    }
}

/*
 *	GUI callbacks
 */

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
 * Control-Q	Quit application
 * Space	Play/Pause/Continue (also Media button "|> ||")
 *
 * Other interesting key names are:
 *	"Left"		Arrow <
 *	"Right"		Arrow >
 *	"Up"		Arrow ^
 *	"Down"		Arrow v
 *	"Prior"		PgUp
 *	"Next"		PgDn
 *	"XF86AudioPlay"	Media button >
 *	"XF86AudioStop"	Media button []
 *	"XF86AudioPrev"	Media button <<
 *	"XF86AudioNext"	Media button >>
 */

static void
keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Key_Down *ev = einfo;
    Evas_Object *em = data;	/* The Emotion object */
    const Evas_Modifier *mods = evas_key_modifier_get(evas);
    Eina_Bool Shift = evas_key_modifier_is_set(mods, "Shift");
    Eina_Bool Control = evas_key_modifier_is_set(mods, "Control");

    /* Control-Q: Quit */
    if (Control && !strcmp(ev->key, "q")) {
	ecore_main_loop_quit();
    } else

    /* Space: Play/Pause/Replay */
    if (!strcmp(ev->key, "space") ||
	!strcmp(ev->key, "XF86AudioPlay")) {
	switch (playing) {
	case PLAYING:
	    pause_playing(em);
	    break;

	case STOPPED:
	    disp_time = 0.0;
	    repaint_display(em);
	    calc_columns(disp_offset, disp_width, em);
	    start_playing(em);
	    break;

	case PAUSED:
	    continue_playing(em);
	    break;
	}
    } else

    /*
     * Arrow <-/->: Jump back/forward a second; with Shift, 10 seconds.
     */
    if (!strcmp(ev->key, "Left")) {
	time_pan_by(em, Shift ? -10.0 : -1.0);
    } else
    if (!strcmp(ev->key, "Right")) {
	time_pan_by(em, Shift ? 10.0 : 1.0);
    } else

    /*
     * Arrow Up/Down: Pan the frequency axis.
     * The argument to freq_pan_by() is a multiplier for min_freq and max_freq
     * With Shift: an octave. without, a semitone
     */
    if (!strcmp(ev->key, "Up")) {
	freq_pan_by(em, Shift ? 2.0 : pow(2.0, 1.0/12));
    } else
    if (!strcmp(ev->key, "Down")) {
	freq_pan_by(em, Shift ? 1/2.0 : 1/pow(2.0, 1/12.0));
    } else

    /* Zoom on the time axis */
    if (!strcmp(ev->key, "x")) {
	time_zoom_by(em, 0.5);
    } else
    if (!strcmp(ev->key, "X")) {
	time_zoom_by(em, 2.0);
    } else

    /* Zoom on the frequency axis */
    if (!strcmp(ev->key, "y")) {
	freq_zoom_by(em, 0.5);
    } else
    if (!strcmp(ev->key, "Y")) {
	freq_zoom_by(em, 2.0);
    } else

    /* Normal zoom-in zoom-out, i.e. both axes. */
    if (Control && !strcmp(ev->key, "plus")) {
	freq_zoom_by(em, 2.0);
	time_zoom_by(em, 2.0);
    } else
    if (Control && !strcmp(ev->key, "minus")) {
	freq_zoom_by(em, 0.5);
	time_zoom_by(em, 0.5);
    } else

    /* Change dynamic range of colour spectrum, like a brightness control.
     * Star should brighten the dark areas, which is achieved by increasing
     * the dynrange;
     * Slash instead darkens them to reduce visibility of background noise.
     */
    if (!strcmp(ev->key, "asterisk")) {
	change_dyn_range(em, 6.0);
    } else
    if (!strcmp(ev->key, "slash")) {
	change_dyn_range(em, -6.0);
    } else

	fprintf(stderr, "Key \"%s\" pressed.\n", ev->key);
}

/* Audio-playing functions */

static void
pause_playing(Evas_Object *em)
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
start_playing(Evas_Object *em)
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
stop_playing(Evas_Object *em)
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

    if (exit_when_played)
	ecore_main_loop_quit();
}

static void
continue_playing(Evas_Object *em)
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
time_pan_by(Evas_Object *em, double by)
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
	start_playing(em);
    }
}

/* Zoom the time axis on disp_time.
 * Only ever done by 2.0 or 0.5 to improve result cache usefulness.
 * The recalculation of every other pixel column should be triggered
 * by repaint_display().
 */
static void
time_zoom_by(Evas_Object *em, double by)
{
    ppsec *= by;
    step = 1 / ppsec;
    repaint_display(em);
}

/* Pan the display on the vertical axis by changing min_freq and max_freq
 * by a factor.
 */
static void
freq_pan_by(Evas_Object *em, double by)
{
    min_freq *= by;
    max_freq *= by;
    repaint_display(em);
}

/* Zoom the frequency axis by a factor, staying centred on the centre.
 * Values > 1.0 zoom in; values < 1.0 zoom out.
 */
static void
freq_zoom_by(Evas_Object *em, double by)
{
    double  centre = sqrt(min_freq * max_freq);
    double   range = max_freq / centre;

    range /= by;
    min_freq = centre / range;
    max_freq = centre * range;

    repaint_display(em);
}

/* Change the colour scale's dyna,ic range, thereby changing the brightness
 * of the darker areas.
 */
static void
change_dyn_range(Evas_Object *em, double by)
{
    /* As min_db is negative, subtracting from it makes it bigger */
    min_db -= by;

    /* min_db should not go positive */
    if (min_db > -6.0) min_db = -6.0;

    repaint_display(em);
}

/*
 *	Audio callbacks
 */

/*
 * The "playback_started" event is useless because in emotion 0.28 it is
 * delivered when playback of audio finishes (!)
 * An alternative would be the "decode_stop" callback but "playback_finished"
 * is delivered first.
 */

static void
playback_finished_cb(void *data, Evas_Object *obj, void *ev)
{
    Evas_Object *em =(Evas_Object *) data;

    stop_playing(em);
}

/*
 * The periodic timer callback that, when playing, scrolls the display by one pixel.
 * When paused, the timer continues to run to update the display in response to
 * seek commands.
 */

static Eina_Bool
timer_cb(void *data)
{
    Evas_Object *em = (Evas_Object *)data;
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

    if (scroll_by == 0) {
	return(ECORE_CALLBACK_RENEW);
    }

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
	calc_columns(0, disp_width - 1, em);
	repaint_display(em);
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
		repaint_column(disp_offset, em);

	    disp_time = new_disp_time;

	    /* Usual case: scrolling the display left to advance in time */
	    memmove(imagedata, imagedata + (4 * scroll_by),
		    imagestride * disp_height - (4 * scroll_by));

	    /* Repaint the right edge */
	    {   int x;
		for (x = disp_width - scroll_by; x < disp_width; x++) {
		    repaint_column(x, em);
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
		repaint_column(disp_offset, em);

	    disp_time = new_disp_time;

	    /* Happens when they seek back in time */
	    memmove(imagedata + (4 * -scroll_by), imagedata,
		    imagestride * disp_height - (4 * -scroll_by));

	    /* Repaint the left edge */
	    {   int x;
		for (x = -scroll_by - 1; x >= 0; x--) {
		    repaint_column(x, em);
		}
	    }
	}

	/* Repaint the green line */
	green_line();

	evas_object_image_data_update_add(image, 0, 0, disp_width, disp_height);
    }

    return(ECORE_CALLBACK_RENEW);
}

/* Repaint the whole display */
static void
repaint_display(Evas_Object *em)
{
    int pos_x;

    for (pos_x=disp_width - 1; pos_x >= 0; pos_x--) {
	repaint_column(pos_x, em);
    }
    green_line();
    evas_object_image_data_update_add(image, 0, 0, disp_width, disp_height);
}

/* Repaint a column of the display from the result cache or paint it
 * with the background colour if it hasn't been calculated yet.
 * Returns TRUE if the result was found in the cache and repainted,
 *	   FALSE if it painted the background color or was off-limits.
 */
static bool
repaint_column(int column, Evas_Object *em)
{
    /* What time does this column represent? */
    double t = disp_time + (column - disp_offset) * step;

    /* The already-calculated result */
    result_t *r;

    if (column < 0 || column >= disp_width) {
	fprintf(stderr, "Repainting column %d\n", column);
	return FALSE;
    }

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */
    if (t >= 0.0 - DELTA && t <= audio_length + DELTA &&
        (r = recall_result(t)) != NULL) {
	paint_column(column, r);
	return TRUE;
    } else {
	/* ...otherwise paint it with the background colour */
	int y;
	unsigned int *p = (unsigned int *)imagedata + column;

	for (y=disp_height - 1; y >= 0; y--) {
            *p = background;
	    p += imagestride / sizeof(*p);
	}

	/* and if it was for a valid time, schedule its calculation */
	if (t >= 0.0 - DELTA && t <= audio_length + DELTA) {
	    calc_columns(column, column, em);
	}

	return FALSE;
    }
}

/* Paint a column for which we have result data */
static void
paint_column(int pos_x, result_t *result)
{
    float *mag;
    int maglen;
    static float max = 1.0;	/* maximum magnitude value seen so far */
    int i;

    maglen = disp_height;
    mag = calloc(maglen, sizeof(*mag));
    if (mag == NULL) {
       fprintf(stderr, "Out of memory in paint_column.\n");
       exit(1);
    }
    max = interpolate(mag, maglen, result->spec, result->speclen,
		     min_freq, max_freq, sample_rate, log_freq);
    result->mag = mag;
    result->maglen = maglen;

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    for (i=maglen-1; i>=0; i--) {
	unsigned int *pixelrow;

	pixelrow = (unsigned int *)&imagedata[imagestride * ((disp_height - 1) - i)];

#if LITTLE_ENDIAN	/* Provided by stdlib.h on Linux-glibc */
	/* Let colormap write directly to the pixel buffer */
	colormap(20.0 * log10(mag[i] / max), min_db,
		 (unsigned char *)(pixelrow + pos_x), gray);
#else
	/* colormap writes to color[] and we swap them to the pixel buffer */
	{   unsigned char color[3];
	    colormap(20.0 * log10(mag[i] / max), min_db, color, gray);
	    pixelrow[pos_x] = (color[0]) | (color[1] << 8) |
-                             (color[2] << 16) | 0xFF000000;
	}
#endif
    }
}

/* Paint the green line */
static void
green_line()
{
    int y;
    unsigned int *p = (unsigned int *)imagedata + disp_offset;

    for (y=disp_height - 1; y >=0; y--) {
	*p = 0xFF00FF00;
	p += imagestride / sizeof(*p);
    }
}

/*
 *	Emotion interface to FFT calculator
 */

static Ecore_Thread *calc_thread;

static void
calc_result(result_t *result)
{
    /* Send result to main loop */
    if (result != NULL)
	ecore_thread_feedback(result->thread, result);
}

static void
calc_heavy(void *data, Ecore_Thread *thread)
{
    calc_t *c = (calc_t *)data;
    c->thread = thread;
    calc(c, calc_result);
}

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

/*
 * This is called from the calculation thread and can be called several
 * times simultaneously.
 */

static void
calc_notify(void *data, Ecore_Thread *thread, void *msg_data)
{
    calc_t   *calc   = (calc_t *)data;
    Evas_Object *em = (Evas_Object *) calc->data;;
    result_t *result = (result_t *)msg_data;
    int pos_x;	/* Where would this column appear in the displayed region? */

    /* The Evas image that we need to write to */

    /* If the time in question is within the displayed region, paint it. */
    /* For now, there is one pixel column per result */
    pos_x = lrint(disp_offset + (result->t - disp_time) * calc->ppsec);

    /* Update the display if the column is in the displayed region
     * and isn't at the green line's position. */
    if (pos_x >= 0 && pos_x < disp_width && pos_x != disp_offset) {
	paint_column(pos_x, result);
	evas_object_image_data_update_add(image, pos_x, 0, 1, disp_height);
    }

    remember_result(result);

    /* To avoid an embarassing pause at the start of the graphics, we wait
     * until the FFT delivers its first result before starting the player.
     */
    if (autoplay && playing == STOPPED) {
	start_playing(em);
    }
}

/*
 * Result cache, inside emotion.c because it wants to access disp_time
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
