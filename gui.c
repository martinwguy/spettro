/*
 * gui.c: Implementation, in the GUI toolkit in use, of the operations we need.
 */

#include "spettro.h"
#include "gui.h"
#include "key.h"
#include "mouse.h"
#include "scheduler.h"
#include "timer.h"
#include "main.h"

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

/* Internal data used to write on the image buffer */
#if EVAS_VIDEO
static Evas_Object *image;
static Ecore_Evas *ee;
static unsigned char *imagedata = NULL;
static int imagestride;		/* How many bytes per screen line ?*/
       Evas_Object *em = NULL;	/* The Emotion or Evas-Ecore object */
#elif SDL_VIDEO
static SDL_Surface *screen;
#endif

#if EVAS_VIDEO
const unsigned background = 0xFF808080;	/* 50% grey */
const unsigned green	  = 0xFF00FF00;
#elif SDL_VIDEO
unsigned background, green;
#endif

#if SDL_MAIN
static int get_next_SDL_event(SDL_Event *event);
#endif
#if ECORE_MAIN
static void ecore_quitGUI(Ecore_Evas *ee EINA_UNUSED);
#endif

/*
 * Initialise the GUI subsystem.
 *
 * "filename" is the name of the audio file, used for window title
 */
void
gui_init(char *filename)
{
#if EVAS_VIDEO
    Evas *canvas;
#endif

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
    ecore_evas_callback_delete_request_set(ee, ecore_quitGUI);
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

    /* Set GUI callbacks */
    evas_object_event_callback_add(image, EVAS_CALLBACK_KEY_DOWN,
				   keyDown, em);
    evas_object_event_callback_add(image, EVAS_CALLBACK_MOUSE_DOWN,
				   mouseDown, em);
    evas_object_event_callback_add(image, EVAS_CALLBACK_MOUSE_UP,
				   mouseUp, em);
    evas_object_event_callback_add(image, EVAS_CALLBACK_MOUSE_MOVE,
				   mouseMove, em);
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

    background	= SDL_MapRGB(screen->format, 0x80, 0x80, 0x80);
    green	= SDL_MapRGB(screen->format, 0x00, 0xFF, 0x00);

    /* Clear the image buffer to the background color */
    if (SDL_FillRect(screen, NULL, background) != 0) {
        fprintf(stderr, "Couldn't fill screen with background color: %s\n",
		SDL_GetError());
        exit(1);
    }
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
}

/* Tell the video subsystem to update the display from the pixel data */
void
gui_update_display()
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
void
gui_update_column(int pos_x)
{
#if EVAS_VIDEO
    evas_object_image_data_update_add(image, pos_x, 0, 1, disp_height);
#elif SDL_VIDEO
    SDL_UpdateRect(screen, pos_x, 0, 1, disp_height);
#endif
}

void
gui_main()
{
#if ECORE_MAIN
    /* Start main event loop */
    ecore_main_loop_begin();
#elif SDL_MAIN
    {
	SDL_Event event;
	enum key key;

	while (get_next_SDL_event(&event)) switch (event.type) {

	case SDL_QUIT:
	    stop_scheduler();
	    stop_timer();
	    exit(0);	/* atexit() calls SDL_Quit() */

	case SDL_KEYDOWN:
	    Shift   = !!(event.key.keysym.mod & KMOD_SHIFT);
	    Control = !!(event.key.keysym.mod & KMOD_CTRL);
	    key = sdl_key_decode(&event);
	    do_key(key);
	    break;

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	    {
		/* To detect Shift and Control states, it looks like we have to
		 * examine the keys ourselves */
		Uint8 *keystate = SDL_GetKeyState(NULL);
		Shift = keystate[SDLK_LSHIFT] || keystate[SDLK_RSHIFT];
		Control = keystate[SDLK_LCTRL] || keystate[SDLK_RCTRL];

		switch (event.button.button) {
		case SDL_BUTTON_LEFT:
		case SDL_BUTTON_RIGHT:
		    do_mouse_button(event.button.x, event.button.y,
				    event.button.button == SDL_BUTTON_LEFT
				    ? LEFT_BUTTON : RIGHT_BUTTON,
				    event.type == SDL_MOUSEBUTTONDOWN
				    ? MOUSE_DOWN : MOUSE_UP);
		}
	    }
	    break;

	case SDL_MOUSEMOTION:
	    do_mouse_move(event.motion.x, event.motion.y);
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
}

#if SDL_MAIN
static int
get_next_SDL_event(SDL_Event *eventp)
{
    /* Prioritise UI events over window refreshes, results and such */
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

/* Stop the GUI main loop so that the program quits */
void
gui_quit()
{
#if ECORE_MAIN
	ecore_main_loop_quit();
#elif SDL_MAIN
	exit(0);	/* atexit() calls SDL_Quit() */
#endif
}

#if ECORE_MAIN
/* Ecore callback for the same */
static void
ecore_quitGUI(Ecore_Evas *ee EINA_UNUSED)
{
    gui_quit();
}
#endif

/* Tidy up ready for exit */
void
gui_deinit()
{
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
}

void
gui_scroll_by(int scroll_by)
{
    if (scroll_by == 0) return;

    if (scroll_by > 0) {
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
    }

    if (scroll_by < 0) {
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
    }
}

/* Fill a pixel column with a single colour, probably "green" or "background" */
void
gui_paint_column(int pos_x, unsigned int color)
{
#if EVAS_VIDEO
    unsigned char *p;	/* pointer to pixel to set */
    int y;

    for (y=disp_height-1,
	 p = (unsigned char *)((unsigned int *)imagedata + pos_x);
	 y >= 0;
	 y--, p += imagestride) {
	    *(unsigned int *)p = color;
    }
#elif SDL_VIDEO
    SDL_Rect rect = {
	pos_x, 0,
	1, disp_height
    };

    SDL_FillRect(screen, &rect, color);
#endif
}

void
gui_lock()
{
#if SDL_VIDEO
    if (SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) != 0 ) {
	fprintf(stderr, "Can't lock screen: %s\n", SDL_GetError());
	return;
    }
#endif
}

void
gui_unlock()
{
#if SDL_VIDEO
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
#endif
}

void
gui_putpixel(int x, int y, unsigned char *color)
{
#if EVAS_VIDEO
    unsigned int *row;	/* of pixels */

    row = (unsigned int *)&imagedata[imagestride * ((disp_height-1) - y)];
    row[x] = (color[0]) | (color[1] << 8) | (color[2] << 16) | 0xFF000000;
#elif SDL_VIDEO

/* Macro derived from http://sdl.beuc.net/sdl.wiki/Pixel_Access's putpixel() */
#define putpixel(surface, x, y, pixel) \
	((Uint32 *)((Uint8 *)surface->pixels + (y) * surface->pitch))[x] = pixel

    /* SDL has y=0 at top */
    putpixel(screen, x, (disp_height-1) - y,
	     SDL_MapRGB(screen->format, color[2], color[1], color[0]));
# endif
}