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
 * Variants:
 * -p	Play the audio file and start scrolling the display immediately
 * -e	Exit when the audio file has finished playing
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
 * Another day, you'll be overlay a time grid, anchored to the sound, with
 * vertical lines marking beats in 50% green and first beats of bar in
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
 * green pencil, which suggests storing the pixel data not the amplitude,
 * and then save it.
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
#include <Emotion.h>

#include <stdlib.h>
#include <math.h>	/* for lrint() */

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
static void	 remember_result(result_t *result);
static result_t *recall_result(double t);
static void	repaint_display(void);
static void	repaint_column(int column);
static void	paint_column(int column, result_t *result);
static void	green_line(void);

/* GUI callbacks */
static void keyDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
static void quitGUI(Ecore_Evas *ee);

/* Audio playing functions */
static void pause_playing(Evas_Object *em);
static void play_from_start(Evas_Object *em);
static void continue_playing(Evas_Object *em);

/* Audio callback functions */
static Ecore_Timer *timer;
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);
static Eina_Bool timer_cb(void *data);

/* FFT calculating thread */
static void calc_heavy(void *data, Ecore_Thread *thread);
static void calc_notify(void *data, Ecore_Thread *thread, void *msg_data);

/*
 * State variables
 */

/* GUI */
static int disp_width	= 640;	/* Size of displayed drawing area in pixels */
static int disp_height	= 480;
static double disp_time	= 0.0; 	/* When in the audio file is the crosshair? */
static int disp_offset	= 320;	/* Crosshair is in which display column?
				 * == disp_width / 2 */
static double min_freq	= 27.5;		/* Range of frequencies to display: */
static double max_freq	= 14080;	/* 9 octaves from A0 to A9 */
static double min_db	= -100.0;	/* Values below this are black */
static double ppsec	= 25.0;		/* pixel columns per second */
static double step	= 1/25.0;	/* time step per column = 1/ppsec */
static double fftfreq	= 5.0;		/* 1/fft size in seconds */
static bool log_freq	= TRUE;		/* Use a logarithmic frequency axis? */
static bool gray	= FALSE;	/* Display in shades of gray? */

/* The colour for uncalculated areas: Alpha 255, RGB gray */
#define background 0xFF808080

/* Internal data used in notify callback to write on the image buffer */
static Evas_Object *image;
static unsigned char *imagedata = NULL;
static int imagestride;

/* Internal data used to remember the FFT result for each pixel value.
 * For now, it's just an array indexing them. When we can zoom this will
 * need redoing. */
static result_t *results = NULL; /* Linked list of result structures */

/* What the audio subsystem is doing. STOPPED means it has never played,
 * PLAYING means it should be playing audio, PAUSED means we've paused it
 * and it remembers when.
 */
static enum { STOPPED, PLAYING, PAUSED } playing = STOPPED;

static audio_file_t *audio_file;
static double	audio_length = 0.0;	/* Length of the audio in seconds */
static double	sample_rate;		/* SR of the audio in Hertz */

/* option flags */
static bool autoplay = FALSE;	/* -p  Start playing the file right away */
static bool exit_when_played = FALSE;	/* -e  Exit when the fils has played */

int
main(int argc, char **argv)
{
    Ecore_Evas *ee;
    Evas *canvas;
    Evas_Object *em;
    Ecore_Thread *thread;

    calc_t calc;	/* What to calculate FFTs for */
    char *filename;

    argv++; argc--;	/* Skip program name */
    while (argc > 0 && argv[0][0] == '-') {
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
	    disp_offset = disp_width / 2;
	    break;
	case 'h':
	    argv++; argc--;	 /* Advance to numeric argument */
	    if ((disp_height = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-h what?\n");
		exit(1);
	    }
	    break;
	default:
usage:	    fputs("Usage: spettro [-p] [-e] [-h n] [-w n] [file.wav]\n\
-p:\tPlay the file right away\n\
-e:\tExit when the audio file has played\n\
-h n:\tSet spectrogram display height to n pixels\n\
-w n:\tSet spectrogram display width to n pixels\n\
The default file is audio.wav\n", stderr);
	    exit(1);
	}
	argv++; argc--;
    }

    filename = (argc > 0) ? (*argv) : "audio.wav";

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
    /* Clear the image buffer to the background colour */
    {	register int i;
	register unsigned long *p = (unsigned long *)imagedata;

	for (i=(imagestride * disp_height) / sizeof(unsigned long);
	     i > 0;
	     i--) {
	    *p++ = background;
	}
    }
    green_line();
    evas_object_image_data_set(image, imagedata);
#if 0
    /* This version gives an image of fixed size in a window of the same size.
     * Resizing the window leaves the image the same size aligned top left.
     */
    evas_object_image_fill_set(image, 0, 0, disp_width, disp_height);
    ecore_evas_resize(ee, disp_width, disp_height);
//ecore_evas_callback_resize_set(ee, _canvas_resize_cb);
#else
    /* This version gives an image that is automatically scaled with the window.
     * If you resize the window, the underlying image remains of the same size
     * and it is zoomed by the window system, giving a thick green line etc.
     */
    evas_object_image_filled_set(image, TRUE);
    ecore_evas_object_associate(ee, image, 0);
#endif
    evas_object_resize(image, disp_width, disp_height);
    evas_object_focus_set(image, EINA_TRUE); /* Without this no keydown events*/

    evas_object_show(image);

    /* Initialize the audio subsystem */

    em = emotion_object_add(canvas);
    if (!em) {
	fputs("Couldn't initialize audio subsystem.\n", stderr);
	exit(1);
    }

    /* Load the audio file for playing */

    emotion_object_init(em, NULL);
    emotion_object_video_mute_set(em, EINA_TRUE);
    if (emotion_object_file_set(em, filename) != EINA_TRUE) {
	fputs("Couldn't load audio file.\n", stderr);
	exit(1);
    }
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

    /* Set audio player callbacks */
    evas_object_smart_callback_add(em, "playback_finished",
				   playback_finished_cb, NULL);

    /* Start FFT calculator */
    calc.audio_file = audio_file;
    calc.length = audio_length;
    calc.sr	= sample_rate;
    calc.from	= 0.0;
    calc.to	= 0.0;
    calc.ppsec  = ppsec;
    calc.speclen= fftfreq_to_speclen(fftfreq, sample_rate);
    calc.window = KAISER;
    calc.data   = em;	/* Needed to start player when calc is ready */

    thread = ecore_thread_feedback_run(
	calc_heavy, calc_notify, NULL, NULL, &calc, EINA_FALSE);
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
	case PLAYING:
	    pause_playing(em);
	    break;

	case STOPPED:
	    play_from_start(em);
	    break;

	case PAUSED:
	    continue_playing(em);
	    break;
	}
    }
}

/* Audio-playing functions */

static void
pause_playing(Evas_Object *em)
{
    emotion_object_play_set(em, EINA_FALSE);
    ecore_timer_freeze(timer);
    playing = PAUSED;
}

static void
play_from_start(Evas_Object *em)
{
    emotion_object_position_set(em, 0.0);
    emotion_object_play_set(em, EINA_TRUE);
    timer = ecore_timer_add(step, timer_cb, NULL);
    disp_time = 0;
    repaint_display();
    playing = PLAYING;
}

static void
continue_playing(Evas_Object *em)
{
    /* Resynchronise the playing position to the display, as emotion
     * seems to stop playing immediatelyi, but throwing away the
     * unplayed part of the currently-playing audio buffer.
     */
    emotion_object_position_set(em, disp_time);
    emotion_object_play_set(em, EINA_TRUE);
    ecore_timer_thaw(timer);
    playing = PLAYING;
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
    // Evas_Object *em = data;	/* The Emotion object */
    playing = STOPPED;
    ecore_timer_del(timer);
    if (exit_when_played)
	ecore_main_loop_quit();
}

static Eina_Bool
timer_cb(void *data)
{
    /* Replace the green line */
    repaint_column(disp_offset);
    /* Scroll the display left by one pixel */
    memmove(imagedata, imagedata+4, imagestride * disp_height - 4);
    /* Repaint the green line */
    green_line();
    /* Repaint the right edge */
    repaint_column(disp_width - 1);

    evas_object_image_data_update_add(image, 0, 0, disp_width, disp_height);

    disp_time += step;

    return(ECORE_CALLBACK_RENEW);
}

/* Repaint the whole display */
static void
repaint_display()
{
    int pos_x;

    for (pos_x=disp_width - 1; pos_x >= 0; pos_x--) {
	repaint_column(pos_x);
    }
    green_line();
    evas_object_image_data_update_add(image, 0, 0, disp_width, disp_height);
}

/* Repaint a column of the display from the result cache or paint it
 * with the background colour if it hasn't been calculated yet.
 */
static void
repaint_column(int column)
{
    /* What time does this column represent? */
    double t = disp_time + (column - disp_offset) * step;

    /* The already-calculated result */
    result_t *r;

    /* If it's a valid time and the column has already been calculated,
     * repaint it from the cache */
    if (t >= 0.0 - DELTA && t < audio_length + DELTA &&
        (r = recall_result(t)) != NULL) {
	paint_column(column, r);
    } else {
	/* ...otherwise paint it with the background colour */
	int y;
	unsigned long *p = (unsigned long *)imagedata + column;
	for (y=disp_height - 1; y >=0; y--) {
            *p = background;
	    p += imagestride / sizeof(unsigned long);
	}
    }
}

static void
paint_column(int pos_x, result_t *result)
{
    float *mag;
    int maglen;
    static float max = 0.0;	/* maximum magnitude value seen so far */
    int i;

    if (result->mag != NULL) {
	mag = result->mag;
	maglen = result->maglen;
    } else {
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
    }

    /* For now, we just normalize each column to the maximum seen so far.
     * Really we need to add max_db and have brightness/contast control.
     */
    for (i=maglen-1; i>=0; i--) {
	unsigned long *pixelrow;
	unsigned char color[3];

	pixelrow = (unsigned long *)
	    &imagedata[imagestride * ((disp_height - 1) - i)];

#if LITTLE_ENDIAN	/* Provided by stdlib.h on Linux-glibc */
	/* Let colormap write directly to the pixel buffer */
	colormap(20.0 * log10(mag[i] / max), min_db,
		 (unsigned char *) (pixelrow + pos_x),
		 gray);
#else
	/* colormap writes to color[] and we write to the pixel buffer */
	colormap(20.0 * log10(mag[i] / max), min_db, color, gray);
	pixelrow[pos_pos_x] = (color[0]) | (color[1] << 8) |
			  (color[2] << 16) | 0xFF000000;
#endif
    }
}

/* Paint the green line */
static void
green_line()
{
    int y;
    unsigned long *p = (unsigned long *)imagedata + disp_offset;

    for (y=disp_height - 1; y >=0; y--) {
	*p = 0xFF00FF00;
	p += imagestride / sizeof(unsigned long);
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
    ecore_thread_feedback(calc_thread, result);
}

static void
calc_heavy(void *data, Ecore_Thread *thread)
{
    calc_thread = thread;
    calc((calc_t *)data, calc_result);
}

static void
calc_notify(void *data, Ecore_Thread *thread, void *msg_data)
{
    calc_t   *calc   = (calc_t *)data;
    Evas_Object *em = (Evas_Object *) calc->data;;
    result_t *result = (result_t *)msg_data;
    int pos_x;	/* Where would this column appear in the displayed region? */

    /* The Evas image that we need to write to */

    /* If the time in question is within the displayed region, paint it */
    /* For now, one pixel column per result */
    pos_x = lrint(disp_offset + (result->t - disp_time) * calc->ppsec);

    /* Update the display if the column if is in the displayed region
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
	play_from_start(em);
    }
}

static void
remember_result(result_t *result)
{
    /* Add at head of list for speed and simplicity */
    result->next = results;
    results = result;
}

/* Return the result for time t at the current speclen
 * or NULL if it hasn't been calculated yet */
static result_t *
recall_result(double t)
{
    result_t *p;

    for (p=results; p != NULL; p=p->next) {
	/* If the time is the same, this is the result we want */
	if (p->t > t - DELTA && p->t < t + DELTA) {
	    return(p);
	}
    }
    return(NULL);
}
