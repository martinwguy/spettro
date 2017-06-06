/*
 * Program: spettro
 *	Play an audio file displaying a scrolling log spectrogram.
 *
 * File: emotion.c
 *	Main routine implemented using Enlightenment's "emotion" interface.
 *
 * The audio file(s) is given as a command-line argument (default: audio.wav).
 * A window should open showing a graphical representation of the audio file:
 * each frame of audio samples is shown as a vertical bar whose colors are
 * taken from the "heat maps" of sndfile-spectrogram or sox.
 *
 * The color at each point represents the energy in the sound at some
 * frequency (band) at a certain moment in time (or for a certain period).
 * The vertical axis, representing frequency, is logarithmic.
 *
 * Compute the whole spectrogram from the start and
 * - display a column when computed if it falls within the displayed region
 * - when scrolling the display, paint new columns if their spectrogram
 *   has already been computed, otherwise leave them black.
 * Does the event loop ave an idle task for the computation?
 *
 * At startup, the start of the piece is in the centre of the window with
 * the first frames of the audio file shown right of center.
 * If you hit play (press 'space'), the audio starts playing and the
 * display scrolls left so that the current playing position remains at the
 * centre of the window. Another space should pause the playback, another
 * make it continue from where it left off. At end of piece, the playback stops;
 * pressing space makes it start again from the beginning.
 *
 * On the left of the spectrogram is a frequency scale in hertz; on the right
 * are the musical notes A0 C1 an so on. Optionally, the ten conventional stave
 * lines are overlayed in white, as can be black and white one-pixel-high lines
 * (preferably with subpixel postioning) at the frequencies of the piano notes.
 *
 * Along the bottom there may be a time scale in seconds.
 *
 * A time grid may also be displayed, anchored to the sound, not the screen,
 * showing beats in 50% green and first beats of bar in 100% green or 50% red.
 * When the bar lines are displayed, the user can drag individual beat lines;
 * the first time they do this, the rest of the beat lines pan. From the second
 * time onward, moving a different bar line stretches the beats between the
 * pointer and the last beat line that they dropped.
 *
 * The user can resize the window, in which case the displayed image is zoomed..
 * If they hit Control-Q or poke the [X] icon in the window's titlebar,
 * the application should quit.
 *
 * We will need two threads:
 * - The calc thread(s) which perform FFTs and report when they're done.
 * - The GUI thread which handles GUI events, starts/stops the audio player,
 *   tells the calc thread what to calculate, receives results and
 *   displays them.
 * See https://docs.enlightenment.org/auto/emotion_main.html
 * See https://www.enlightenment.org/program_guide/threading_pg
 *
 * The Emotion API notifies events by Evas Object Smart Callbacks in e17:
 * "open_done" when the audio file has been opened successfully, then
 * "playback_started" (!), "decode_stop" and "playback_finished" are
 * all delivered at once when playback finishes.
 *
 * We need to be able to
 * - set the playback position (with emotion_object_position_set())
 * - start/stop/pause/resume playback (with emotion_object_play_set())
 * - start scrolling the display when we start the audio playing
 * - react to playback_finished to stop scrolling
 * - scroll the display in real time, hoping it remains in sync with the music
 * - reposition the player and the display when they pan (drag or arrow keys)
 * and for the spectrogram display we need
 * - to know the sample rate and length of the piece
 * - to be able to get a buffer of its decoded sample values
 *
 * Interesting Emotion calls are:
 * void   emotion_object_play_set(obj, Bool); // Play/Pause/Continue
 * Bool   emotion_object_play_get(obj);
 * void   emotion_object_position_set(obj, double); // in seconds
 * double emotion_object_position_get(obj);       // in seconds
 * Bool   emotion_object_seekable_get(obj);	  // Can you set position?
 * double emotion_object_play_length_get(obj);	  // in seconds
 *	// Returns 0 if called before "length_change" signal has been emitted.
 *
 * Enlightenment's "Convenience audio interface"
 * https://docs.enlightenment.org/stable/efl/group__Ecore__Audio__Group.html
 * has stuff to get piece length and sample rate but it doesn't compile
 * on Debian e17 ("Can't find <Ecore_audio_in_pulse.h>" and, bypassed that,
 * it fails to find libecore-audio.)
 *
 * What I really want is an audio system that I can pass two buffers'
 * worth of audio to and have it notify me when it has played the first
 * and is playing the second so that I can prepare the following buffer
 * of audio for it.
 *
 * Tizen has OpenAL as a lower-level audio system, which uses pulseaudio.
 * It's interface is what I want.
 *
 * Status:
 *    Audio playback works with ALSA if the JACK server isn't running.
 *    A black rectangular window is displayed and remains until you quit.
 *
 * Bugs:
 *    -	It doesn't display anything yet.
 *    - Playback is sometimes interrupted by clicks of silence.
 *    - If the JACK server is running with or without pulseaudio
 *	it doesn't play the audio and says:
ERR<1826>:emotion-gstreamer[T:1110379328]
modules/emotion/gstreamer/emotion_gstreamer.c:1679
_eos_sync_fct() ERROR from element wavparse0: Internal data flow error.
ERR<1826>:emotion-gstreamer[T:1110379328]
modules/emotion/gstreamer/emotion_gstreamer.c:1680
_eos_sync_fct() Debugging info: gstwavparse.c(2110): gst_wavparse_loop ():
/GstPlayBin2:playbin/GstURIDecodeBin:uridecodebin0/GstDecodeBin2:decodebin20/GstWavParse:wavparse0: streaming task paused, reason not-linked (-1)
ERR<1826>:emotion-gstreamer modules/emotion/gstreamer/emotion_gstreamer.c:1760
_emotion_gstreamer_video_pipeline_parse() Unable to get GST_CLOCK_TIME_NONE.
 *
 *	Martin Guy <martinwguy@gmail.com>, Dec 2016-Jan 2017.
 */

#define EFL_EO_API_SUPPORT
#define EFL_BETA_API_SUPPORT

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Evas.h>
#include <Emotion.h>

#include <audiofile.h>	/* Needed only to find out sample rate! */

#include <math.h>	/* for lrint() */

#include "calc.h"

#define Eo_Event void

/*
 * Prototypes for callback functions
 */

/* Helper functions */
static int fftfreq_to_speclen(double fftfreq, double sr);

/* GUI */
static void keyDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void quitGUI(Ecore_Evas *ee);
static void open_done_cb(void *data, Evas_Object *obj, void *ev);

/* Audio player */
static Ecore_Timer *timer;
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);
static Eina_Bool timer_cb(void *data);

/* FFT calculating thread */
static void calc_notify(void *data, Ecore_Thread *thread, void *msg_data);
static void calc_cancel(void *data, Ecore_Thread *thread);
static void calc_end(void *data, Ecore_Thread *thread);

/*
 * State variables
 */

/* GUI */
int disp_width = 320;	/* Size of displayed drawing area in pixels */
int disp_height = 200;
double disp_time = 0.0; /* When in the audio file is at the crosshair? */
int disp_offset = 160; /* Crosshair in which dispay column? */

/* What the audio subsystem is doing. STOPPED means it has never played,
 * PLAYING means it should be playing audio, PAUSED means we've paused it
 * and it remembers when.
 */
static enum { STOPPED, PLAYING, PAUSED } playing = STOPPED;

static double	audio_length = -1.0;	/* Length of the audio in seconds */
static double	sample_rate;		/* SR of the audio in Hertz */
static AFfilehandle af;			/* audio file opened by libaudiofile */

/*
 * start_time is ecore_time_get()'s value when the piece started playing.
 * If we are paused, pause_time is the time at which the pause happened.
 * If they then restart the piece, we recalculate start_time to when it
 * would have had to start to be playing now at the pause position.
 */
static double start_time;
static double pause_time;

int
main(int argc, char **argv)
{
    Ecore_Evas *ee;
    Evas *canvas;
    Evas_Object *em;
    Evas_Object *image;
    Ecore_Thread *thread;

    double ppsec = 10.0;
    double fftfreq = 10.0;

    calc_t calc;	/* What to calculate FFTs for */
    char *filename = (argc > 1) ? argv[1] : "audio.wav";

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

    image = evas_object_image_add(canvas);
    evas_object_image_filled_set(image, EINA_TRUE);

    /* Propagate resize events from the container to the image */
    ecore_evas_object_associate(ee, image, 0);
    evas_object_resize(image, disp_width, disp_height);
    evas_object_focus_set(image, EINA_TRUE); /* Without this no keydown events*/
    evas_object_show(image);

    /* Initialize the audio subsystem */

    em = emotion_object_add(canvas);
    if (!em) {
	fputs("Couldn't initialize audio subsystem.\n", stderr);
	exit(1);
    }

    /* Load the audio file */

    emotion_object_init(em, NULL);
    emotion_object_video_mute_set(em, EINA_TRUE);
    if (emotion_object_file_set(em, filename) != EINA_TRUE) {
	fputs("Couldn't load audio file.\n", stderr);
	exit(1);
    }
    evas_object_show(em);

    /* Find out the sample rate of the file. emotion seems incapable of this
     * and doesn't know the file length until open_done_cb() is called.
     */
    {
	AFframecount frame_count;	/* Number of sample frames */

	af = afOpenFile(filename, "r", NULL);
	if (af == NULL) {
	    fprintf(stderr, "libaudiofile failed to open the file.\n");
	    goto quit;
	}
	sample_rate = afGetRate(af, AF_DEFAULT_TRACK);
	frame_count = afGetFrameCount(af, AF_DEFAULT_TRACK);
	audio_length = (double) frame_count / sample_rate;
    }

    /* Set GUI callbacks */
    evas_object_event_callback_add(image, EVAS_CALLBACK_KEY_DOWN, keyDown, em);

    /* Set audio player callbacks */
    evas_object_smart_callback_add(em, "open_done", open_done_cb, em);
    evas_object_smart_callback_add(em, "playback_finished",
				   playback_finished_cb, NULL);

    /* Start FFT calculator */
    calc.af	= af;
    calc.length = audio_length;
    calc.sr	= sample_rate;
    calc.from	= 0.0;
    calc.to	= 0.0;
    calc.ppsec  = ppsec;
    calc.speclen= fftfreq_to_speclen(fftfreq, sample_rate);
    calc.window = KAISER;

    thread = ecore_thread_feedback_run(
	calc_heavy, calc_notify, calc_end, calc_cancel, &calc, EINA_FALSE);
    if (thread == NULL) {
	fprintf(stderr, "Can't start FFT-calculating thread.\n");
	goto quit;
    }

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

/*
 *	Helper functions
 */

static bool is_good_speclen(int n);

/* Choose a suitable value for speclen (== fftsize/2). */
static int
fftfreq_to_speclen(double fftfreq, double sr)
{
    int speclen = (sr / fftfreq + 1) / 2;
    int d; /* difference between ideal speclen and preferred speclen */

    /* Find the nearest fast value for the FFT size. */

    for (d = 0 ; /* Will terminate */ ; d++) {
	/* Logarithmically, the integer above is closer than
	 * the integer below, so prefer it to the one below.
	 */
	if (is_good_speclen(speclen + d)) {
	    speclen += d;
	    break;
	}
	if (is_good_speclen(speclen - d)) {
	    speclen -= d;
	    break;
	}
    }

    return speclen;
}

/*
 * Helper function: is N a "fast" value for the FFT size?
 *
 * We use fftw_plan_r2r_1d() for which the documentation
 * http://fftw.org/fftw3_doc/Real_002dto_002dReal-Transforms.html says:
 *
 * "FFTW is generally best at handling sizes of the form
 *      2^a 3^b 5^c 7^d 11^e 13^f
 * where e+f is either 0 or 1, and the other exponents are arbitrary."
 *
 * Our FFT size is 2*speclen, but that doesn't affect these calculations
 * as 2 is an allowed factor and an odd fftsize may or may not work with
 * the "half complex" format conversion in calc_magnitudes().
 */
static bool is_2357(int n);

static bool
is_good_speclen (int n)
{
    /* It wants n, 11*n, 13*n but not (11*13*n)
    ** where n only has as factors 2, 3, 5 and 7
     */
    if (n % (11 * 13) == 0) return 0; /* No good */

    return is_2357(n) || ((n % 11 == 0) && is_2357(n / 11))
		      || ((n % 13 == 0) && is_2357(n / 13));
}

/* Helper function: does N have only 2, 3, 5 and 7 as its factors? */
static bool
is_2357(int n)
{
    /* Eliminate all factors os 2, 3, 5 and 7 and see if 1 remains */
    while (n % 2 == 0) n /= 2;
    while (n % 3 == 0) n /= 3;
    while (n % 5 == 0) n /= 5;
    while (n % 7 == 0) n /= 7;
    return (n == 1);
}

/*
 *	GUI callbacks
 */

/* Quit on window close or Control-Q */
static void
quitGUI(Ecore_Evas *ee EINA_UNUSED)
{
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

    /* Control-Q: Quit */
    if (strcmp(ev->key, "q") == 0
	&& evas_key_modifier_is_set(mods, "Control")) {
	ecore_main_loop_quit();
    } else

    /* Space: Play/Pause */
    if (strcmp(ev->key, "space") == 0 ||
	strcmp(ev->key, "XF86AudioPlay") == 0) {
	switch (playing) {
	    double now;

	case PLAYING:
	    emotion_object_play_set(em, EINA_FALSE);
	    pause_time = ecore_time_get();
	    playing = PAUSED;
	    ecore_timer_freeze(timer);
	    break;

	case STOPPED:
	    emotion_object_position_set(em, 0.0);
	    start_time = ecore_time_get();
	    emotion_object_play_set(em, EINA_TRUE);
	    timer = ecore_timer_add(0.1, timer_cb, em);
	    playing = PLAYING;
	    break;

	case PAUSED:
	    emotion_object_play_set(em, EINA_TRUE);
	    now = ecore_time_get();
	    /* When it would have had to start to be synchronised with the
	     * restarted audio. */
	    start_time += now - pause_time;
	    playing = PLAYING;
	    ecore_timer_thaw(timer);
	    break;
	}
    }
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
open_done_cb(void *data, Evas_Object *obj, void *ev)
{
    // Evas_Object *em = data;	/* The Emotion object */

    /* We could set audio_length here. Its report differs
     * from libaudiofile's calculation by 1e-16 */
    // new = emotion_object_play_length_get(em);
}

static void
playback_finished_cb(void *data, Evas_Object *obj, void *ev)
{
    // Evas_Object *em = data;	/* The Emotion object */
    playing = STOPPED;
    ecore_timer_del(timer);
}

static Eina_Bool
timer_cb(void *data)
{
    // Evas_Object *em = data;	/* The Emotion object */

    /* TODO: Scroll the display by one pixel */

    return(ECORE_CALLBACK_RENEW);
}

/*
 *	FFT calculator callbacks
 */

static void
calc_notify(void *data, Ecore_Thread *thread, void *msg_data)
{
    calc_t   *calc   = (calc_t *)data;
    result_t *result = (result_t *)msg_data;
    int pos_x;	/* Where would this column appear in the displayed region? */

    /* If the time in question is within the displayed region, paint it */
    /* For now, one pixel column per result */
    pos_x = lrint(disp_offset + (result->t - disp_time) * calc->ppsec);

    if (pos_x > 0 && pos_x < disp_width) {
	fprintf(stderr, "display at column %d\n", pos_x);
    }

    /* Cache the FFT result */
}

static void
calc_cancel(void *data, Ecore_Thread *thread)
{
    fprintf(stderr, "calc_cancel()\n");
}

static void
calc_end(void *data, Ecore_Thread *thread)
{
    fprintf(stderr, "calc_end()\n");
}
