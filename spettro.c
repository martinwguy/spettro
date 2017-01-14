/*
 * spettro.c: Play an audio file displaying a scrolling log spectrogram.
 *
 * The audio file(s) is given as a command-line argument (default: audio.wav).
 * A window should open showing a graphical representation of the audio file:
 * each frame of audio samples is shown as a vertical bar whose color is
 * taken from the "heat map" color map of sndfile-spectrogram or sox.
 *
 * The color at each point represents the energy in the sound at some
 * frequency (band) at a certain moment in time (or for a certain period).
 * The vertical axis, representing frequency, is logarithmic.
 *
 * First hack:
 *
 * Display a spectrogram of whole piece, resized to the size of the window.
 * Hit space to make it play/pause/continue or start again from the beginning.
 * When it is playing, a vertical green line moves across the spectrogram
 * to show the playback position.
 *
 * Second hack:
 *
 * At startup, the start of the piece is in the centre of the window with
 * the first frames of the audio file represented shown right of center.
 * If you hit play (press 'space'), the audio starts playing and the
 * display scrolls left so that the current playing position remains at the
 * centre of the window. Another space should pause the playback, another
 * make it continue from where it left off. At end of piece, the playback stops;
 * pressing space makes it start again from the beginning.
 *
 * On the left of the spectrogram is a frequency scale in hertz; on the right
 * are the musical notes A0 C1 an so on. Optionally, the ten conventional stave
 * lines are overlayed in white, as can be black and white one-pixel-high lines
 * (preferably with subpixel postioning) at the frequencies of the piano notes
 * or the six guitar strings.
 *
 * A time scale may also be display, anchored to the sound, not the screen,
 * showing beats in 50% green and first beats of bar in 100% green or 50% red.
 * When the bar lines are displayed, the user can grag individual beat lines;
 * the first time they do this, the rest of the beat lines pan. From the second
 * time onward, moving a different bar line stretches the beats between the
 * pointer and the last beat line that they dropped.
 *
 * The user can resize the window, in which case the displayed image is zoomed..
 * If they hit Control-Q or poke the [X] icon in the window's titlebar,
 * the application should quit.
 *
 * The Emotion API notifies events by Evas Object Smart Callbacks in e17:
 *
 * When the audio file has been opened successfully, event "open_done"
 * then, when playback finishes, "playback_started" (!), "decode_stop"
 * and "playback_finished" are all delivered at once.
 *
 * Of these, we need to be able to
 * - set the playback position (with emotion_object_position_set()?)
 * - start/stop/pause/resume playback
 * - start scrolling the display when we start the audio playing
 * - react to playback_finished to stop scrolling
 * - react to position_update and emit it when they drag to pan in time
 *   (if that works... test it.)
 *
 * We need two threads:
 * - The calc thread(s) which perform FFTs and report when they're done.
 * - The GUI thread which handles GUI events, starts/stops the audio player,
 *   tells the calc thread what to calculate, receives results and
 *   displays them.
 * See https://docs.enlightenment.org/auto/emotion_main.html
 * See https://www.enlightenment.org/program_guide/threading_pg
 * Threads: ecore_thread_feedback_run()

 *	What I really want is an audio system that I can pass two buffers'
 *	worth of audio to and have it notify me when it has played the first
 * 	and is playing the second so that I can prepare the following buffer
 *	of audio for it.
 *	Let's see how gstreamer does this, and if we can use that directly.
 *
 * Status:
 *    Audio playback works with ALSA if the JACK server isn't running.
 *    A black rectangular window is displayed and remains until you quit.
 *
 * TODO:
 *    - See if we can pause/resume the audio with [space] and see what events
 *	are emitted (decode_stop?).
 *
 * Bugs:
 *    -	It doesn't display anything yet.
 *    - Playback is sometimes interrupted by clicks of silence.
 *    - If the JACK server is running with or without pulseaudio too,
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
 *    - If you kill it with Control-C, it dumps core.
 *
 *	Martin Guy <martinwguy@gmail.com>, Dec 2016-Jany 2017.
 */

#define EFL_EO_API_SUPPORT
#define EFL_BETA_API_SUPPORT

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Evas.h>
#include <Emotion.h>

#define Eo_Event void

/* Prototypes for callback functions */

static void keyDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void quitGUI(Ecore_Evas *ee);
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);
static void position_update_cb(void *data, Evas_Object *obj, void *ev);
static void decode_stop_cb(void *data, Evas_Object *obj, void *ev);

/* State variables */

/* What the audio subsystem is doing. STOPPED means it has never played,
 * PLAYING means it should be playing audio, PAUSED means we've paused it
 * and it remembers the current playback position.
 */
static enum { STOPPED, PLAYING, PAUSED } playing = STOPPED;

static double audio_length;	/* Length of the audio in seconds */

/* start_time is ecore_time_get()'s value when the piece started playing.
 * If we are paused, pause_time is the time at which the pause happened.
 * If they then restart the piece, we recalculate start_time to when it
 * would have had to start playing uninterruptedly to be at the current point.
 */
static start_time;
static pause_time;

int
main(int argc, char **argv)
{
    Ecore_Evas *ee;
    Evas *canvas;
    Evas_Object *em;
    Evas_Object *image;
    char *filename = (argc > 1) ? argv[1] : "audio.wav";
    int w, h;

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

    evas_object_show(image);

    /* Propagate resize events from the container to the image */
    ecore_evas_object_associate(ee, image, 0);
    evas_object_resize(image, 320, 200);
    evas_object_focus_set(image, EINA_TRUE); /* Without this no keydown events*/

    /* Initialize the audio subsystem */

    em = emotion_object_add(canvas);
    if (!em) {
	fputs("Couldn't initialize audio subsystem.\n", stderr);
	exit(1);
    }
    emotion_object_init(em, NULL);
    emotion_object_video_mute_set(em, EINA_TRUE);
    if (emotion_object_file_set(em, filename) != EINA_TRUE) {
	fputs("Couldn't load audio file.\n", stderr);
	exit(1);
    }
    evas_object_show(em);

    evas_object_event_callback_add(image, EVAS_CALLBACK_KEY_DOWN, keyDown, em);
    evas_object_smart_callback_add(em, "playback_finished", playback_finished_cb, NULL);
    evas_object_smart_callback_add(em, "position_update", position_update_cb, NULL);
    evas_object_smart_callback_add(em, "decode_stop", decode_stop_cb, NULL);

    ecore_main_loop_begin();

    ecore_evas_free(ee);
    ecore_evas_shutdown();

    return 0;
}

/* Quit on Control-Q */
static void
quitGUI(Ecore_Evas *ee EINA_UNUSED)
{
    ecore_main_loop_quit();
}

/*
 * Audio callbacks
 */

/* The "playback_started" event is useless because in emotion 0.28 it is
 * delivered when playback of audio finishes (!) */

static void
playback_finished_cb(void *data, Evas_Object *obj, void *ev)
{
    playing = STOPPED;
}

static void
position_update_cb(void *data, Evas_Object *obj, void *ev)
{
    //printf("position_update\n");
}

static void
decode_stop_cb(void *data, Evas_Object *obj, void *ev)
{
    //printf("decode_stop\n");
}

/*
 * Keypress events (GUI control)
 */

static void
keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Key_Down *ev = einfo;
    Evas_Object *em = data;	/* The Emotion object */
    const Evas_Modifier *mods = evas_key_modifier_get(evas);

    /* Control-Q: Quit */
    if (strcmp(ev->key, "q") == 0)
	&& evas_key_modifier_is_set(mods, "Control") {
	ecore_main_loop_quit();
    } else

    /* Space: Play/Pause */
    if (strcmp(ev->key, "space") == 0 ||
	strcmp(ev->key, "XF86AudioPlay") == 0) {
	switch (playing) {
	case PLAYING:
	    emotion_object_play_set(em, EINA_FALSE);
	    playing = PAUSED;
	    break;
	case STOPPED:
	    emotion_object_position_set(em, 0.0);
	case PAUSED:
	    emotion_object_play_set(em, EINA_TRUE);
	    playing = PLAYING;
	    break;
	}
    } else
// XF86AudioPrev XF86AudioNext
/*
"Left"	Arrow
"Right"	Arrow
"Up"	Arrow
"Down"	Arrow
"Prior"	PgUp
"Next"	PgDn
*/
        printf("[%s]\n", ev->key);
}
