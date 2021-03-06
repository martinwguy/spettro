/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * gui.c: Implementation, in the GUI toolkit in use, of the operations we need.
 */

#include "spettro.h"
#include "gui.h"

#include "audio.h"
#include "axes.h"
#include "key.h"
#include "mouse.h"
#include "paint.h"	/* for do_scroll() */
#include "scheduler.h"
#include "timer.h"
#include "ui.h"

/* Libraries' header files. See config.h for working combinations of defines */

#include <png.h>
#include <errno.h>

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
static size_t imagestride;		/* How many bytes per screen line ?*/
       Evas_Object *em = NULL;	/* The Emotion or Evas-Ecore object */
#elif SDL_VIDEO
# if SDL1
static SDL_Surface *screen;
# elif SDL2
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Surface *screen;
# endif
#endif

#if EVAS_VIDEO
const color_t gray	= 0xFF808080;
const color_t green	= 0xFF00FF00;
const color_t white	= 0xFFFFFFFF;
const color_t black	= 0xFF000000;
#elif SDL_VIDEO
color_t gray, green, white, black;
#endif

#if SDL_MAIN
static int get_next_SDL_event(SDL_Event *event);
#endif
#if ECORE_MAIN
static void ecore_quitGUI(Ecore_Evas *ee EINA_UNUSED);
#endif

/*
 * Initialize the GUI subsystem.
 *
 * "filename" is the name of the audio file, used for window title
 */
void
gui_init(char *filename)
{
    static char *last_filename = NULL;
#if EVAS_VIDEO
    Evas *canvas;
#endif

    if (filename == NULL) filename = last_filename;
    else last_filename = filename;

    /*
     * Initialize the various subsystems
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
#  if SDL1
	flags |= SDL_INIT_EVENTTHREAD;
#  elif SDL2
	flags |= SDL_INIT_EVENTS;  /* Also comes free with SDL_INIT_VIDEO */
#  endif
# endif
	if (SDL_Init(flags) != 0) {
	    fprintf(stderr, "Couldn't initialize SDL: %s.\n", SDL_GetError());
	    exit(1);
	}
	atexit(SDL_Quit);

	/* For some reason, SDL1.2 disables key repeat */
# if SDL1
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
			    SDL_DEFAULT_REPEAT_INTERVAL);
# endif
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
    if (fullscreen) ecore_evas_fullscreen_set(ee, TRUE);
    ecore_evas_show(ee);

    canvas = ecore_evas_get(ee);

    /* Create the image and its memory buffer */
    image = evas_object_image_add(canvas);
    evas_object_image_colorspace_set(image, EVAS_COLORSPACE_ARGB8888);
    evas_object_image_size_set(image, disp_width, disp_height);
    imagestride = evas_object_image_stride_get(image);
    imagedata = Malloc(imagestride * disp_height);

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

    /* Set GUI callbacks at a lower priority so that scroll events come first */
    evas_object_event_callback_priority_add(image, EVAS_CALLBACK_KEY_DOWN,
				   EVAS_CALLBACK_PRIORITY_AFTER, keyDown, em);
    evas_object_event_callback_priority_add(image, EVAS_CALLBACK_MOUSE_DOWN,
				   EVAS_CALLBACK_PRIORITY_AFTER, mouseDown, em);
    evas_object_event_callback_priority_add(image, EVAS_CALLBACK_MOUSE_UP,
				   EVAS_CALLBACK_PRIORITY_AFTER, mouseUp, em);
    evas_object_event_callback_priority_add(image, EVAS_CALLBACK_MOUSE_MOVE,
				   EVAS_CALLBACK_PRIORITY_AFTER, mouseMove, em);
#endif

#if SDL_VIDEO
# if SDL1
    /* "Use SDL_SWSURFACE if you plan on doing per-pixel manipulations,
     * or blit screens with alpha channels, and require a high framerate."
     * "SDL_DOUBLEBUF is only valid when using HW_SURFACE."
     *     -- http://sdl.beuc.net/sdl.wiki/SDL_SetVideoMode
     * We could be more permissive about bpp, but 32 will do for a first hack.
     */
    screen = SDL_SetVideoMode(disp_width, disp_height, 32,
			      SDL_SWSURFACE | (fullscreen ? SDL_FULLSCREEN : 0));
	/* | SDL_RESIZEABLE one day */
    if (screen == NULL) {
        fprintf(stderr, "Couldn't create window: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_WM_SetCaption(filename, NULL);
# elif SDL2
    window = SDL_CreateWindow(filename,
    			      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			      disp_width, disp_height,
			      fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
				 /* | SDL_WINDOW_RESIZABLE */
    if (window == NULL) {
        fprintf(stderr, "Couldn't create window: %s\n", SDL_GetError());
        exit(1);
    }
    if (fullscreen) {
    	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	if (w <= 0 || h <= 0) {
	    fprintf(stderr, "Image too big for SDL.\n");
	    exit(1);
	}
	disp_width = w; disp_height = h;
    }
    /* Fullscreen can give different dimensions from window mode
     * so recalculate dependent variables.  Wot a mess!
     */
    max_x = disp_width - 1;
    disp_offset = disp_width / 2;
    if (show_freq_axes) max_x -= note_name_axis_width;
    max_y = disp_height - 1;
    if (show_time_axes) max_y -= top_margin;
    /* maglen depends on max_y */

    renderer = SDL_CreateRenderer(window, -1, 0);
    	/* maybe SDL_RENDERER_PRESENTVSYNC */
    if (renderer == NULL) {
        fprintf(stderr, "Couldn't create renderer: %s\n", SDL_GetError());
        exit(1);
    }

    screen = SDL_GetWindowSurface(window);
# endif

    background	= RGB_to_color(0x80, 0x80, 0x80);	/* 50% gray */
    green	= RGB_to_color(0x00, 0xFF, 0x00);
    white	= RGB_to_color(0xFF, 0xFF, 0xFF);
    black	= RGB_to_color(0x00, 0x00, 0x00);

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
# if SDL1
    SDL_UpdateRect(screen, 0, 0, 0, 0);
# elif SDL2
    SDL_UpdateWindowSurface(window);
# endif
#endif
}

/* Tell the video subsystem to update a rectangle from the pixel data
 * The parameters are in our 0-at-bottom coordinates;
 * while Evas and SDL both have 0-at-top.
 */
void
gui_update_rect(int from_x, int from_y, int to_x, int to_y)
{
    int width = to_x - from_x + 1;
    int height = to_y - from_y + 1;

#if EVAS_VIDEO
    evas_object_image_data_update_add(image, from_x,
	(disp_height - 1) - (from_y + height - 1), width, height);
#elif SDL_VIDEO
# if SDL1
    SDL_UpdateRect(screen, from_x,
	(disp_height - 1) - (from_y + height - 1), width, height);
# elif SDL2
    {
    	SDL_Rect rect;
	rect.x = from_x;
	/* Our Y coordinates have their origin at the bottom, SDL at the top */
	rect.y = (disp_height - 1) - (from_y + height - 1);
	rect.w = width;
	rect.h = height;
	if (SDL_UpdateWindowSurfaceRects(window, &rect, 1) != 0) {
	    fprintf(stderr, "SDL_UpdateWindowSUrfaceRects failed: %s\n",
		    SDL_GetError());
	}
    }
# endif
#endif
}

/* Tell the video subsystem to update one column of the display
 * from the pixel data
 */
void
gui_update_column(int pos_x)
{
    gui_update_rect(pos_x, min_y, pos_x, max_y);
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

# if SDL2
	/* Use SDL2's TEXTINPUT mode so that keyboard mapping with Shift and
	 * AltGr works */
	SDL_StartTextInput();
# endif
	while (get_next_SDL_event(&event)) switch (event.type) {
# if SDL2
	case SDL_WINDOWEVENT:
	    if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
		gui_update_display();
	    break;
# endif

	case SDL_QUIT:
	    return;

	/* For SDL2, we enable both KEYDOWN and TEXTINPUT because
	 * TEXTINPUT handles Shift and AltGr to get difficult chars on
	 * international keyboards, but ignores arrow keys and the keypad.
	 * in key.c, if SDL2, we process most keys with TEXTINPUT and only
	 * the ones TEXTINPUT ignores in response to KEYDOWN.
	 */

	case SDL_KEYDOWN:
	    /* SDL's event.key.keysym.mod reflects the state of the modifiers
	     * at initial key-down. SDL_GetModState seems to reflect now. */
	    Shift = !!(SDL_GetModState() & KMOD_SHIFT);
	    Ctrl  = !!(SDL_GetModState() & KMOD_CTRL);
	    sdl_keydown(&event);
	    break;
# if SDL2
	case SDL_TEXTINPUT:
	    Shift = !!(SDL_GetModState() & KMOD_SHIFT);
	    Ctrl  = !!(SDL_GetModState() & KMOD_CTRL);
	    sdl_keydown(&event);
	    break;
# endif

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	    {
		/* To detect Shift and Ctrl states, it looks like we have to
		 * examine the keys ourselves */
# if SDL1
		SDLMod
# elif SDL2
		SDL_Keymod
# endif
			   state = SDL_GetModState();
		Shift = !!(state & KMOD_SHIFT);
		Ctrl = !!(state & KMOD_CTRL);

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

# if SDL1
	case SDL_VIDEORESIZE:
	    /* One day */
	    break;
# endif

	case SDL_USEREVENT:
	    switch (event.user.code) {
	    case RESULT_EVENT:
		/* Column result from a calculation thread */
		calc_notify((calc_t *) event.user.data1);
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
    /* Prioritize UI events over window refreshes, results and such */
    /* First, see if there are any UI events to be had */
    SDL_PumpEvents();
#if SDL1
    /* First priority: Quit */
    if (SDL_PeepEvents(eventp, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_QUIT)) == 1)
        return 1;

    /* Second priority: screen-scrolling events */
    if (SDL_PeepEvents(eventp, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_USEREVENT)) == 1)
	return 1;

    /* Third priority: UI events */
    if (SDL_PeepEvents(eventp, 1, SDL_GETEVENT,
			     SDL_EVENTMASK(SDL_KEYDOWN) |
			     SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN) |
			     SDL_EVENTMASK(SDL_MOUSEBUTTONUP) |
			     SDL_EVENTMASK(SDL_MOUSEMOTION)) == 1) {
	/* Only action the last mousemove event, to avoid redrawing bar lines
	 * for every pixel move, which gets slow when the barlines are close.
	 */
	do {} while (SDL_PeepEvents(eventp, 1, SDL_GETEVENT,
				    SDL_EVENTMASK(SDL_MOUSEMOTION)) == 1);
	return 1;
    }
#elif SDL2
    /* First priority: Quit */
    if (SDL_PeepEvents(eventp, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) == 1)
	return 1;

    /* Second priority: screen-scrolling events */
    if (SDL_PeepEvents(eventp, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) == 1)
	return 1;

    /* Third priority: UI events
     *
     * SDL_{KEYDOWN,KEYUP,TEXEDITING,TEXTINPUT} are consecutive and followed by
     * SDL_MOUSE{MOTION,BUTTONDOWN,BUTTONUP,WHEEL}.
     * see /usr/include/SDL2/SDL_events.h
     */
    while (SDL_PeepEvents(eventp, 1, SDL_GETEVENT,
    				     SDL_KEYDOWN, SDL_MOUSEWHEEL) == 1) {
	switch (eventp->type) {
	case SDL_KEYDOWN:
	case SDL_TEXTINPUT:
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	    return 1;
	case SDL_MOUSEMOTION:
	    /* Only action the last mousemove event, to avoid redrawing bar lines
	     * for every pixel move, which gets slow when the barlines are close.
	     */
	    do {} while (SDL_PeepEvents(eventp, 1, SDL_GETEVENT,
					SDL_MOUSEMOTION, SDL_MOUSEMOTION) == 1);
	    return 1;
	default:
	    /* Ignore keyboard/mouse events that are not known to gui_main() */
	    continue;
	}
    }
#endif

    /* No Quit or UI events? Wait for all events */
    return SDL_WaitEvent(eventp);
}
#endif

/* Stop everything before we exit */
void
gui_quit()
{
    if (playing == PLAYING) {
	stop_playing();
    }
}

#if ECORE_MAIN
/* Ecore callback for the same */
static void
ecore_quitGUI(Ecore_Evas *ee EINA_UNUSED)
{
    gui_quit_main_loop();
}
#endif

void
gui_quit_main_loop(void)
{
#if ECORE_MAIN
    ecore_main_loop_quit();
#elif SDL_MAIN
    SDL_Event event;
    event.type = SDL_QUIT;
    if (SDL_PushEvent(&event) != SDL_PUSHEVENT_SUCCESS) {
	fprintf(stderr, "sdl_main_loop_quit event push failed: %s\n", SDL_GetError());
	exit(1);
    }
#endif
}

/* Tidy up ready for exit */
void
gui_deinit()
{
#if EVAS_VIDEO
    ecore_evas_free(ee);
    /* This makes it dump core or barf error messages about bad magic */
    ecore_evas_shutdown();
#endif
#if EVAS_VIDEO
    free(imagedata);
#endif

#if SDL_VIDEO
# if SDL2
    SDL_DestroyWindow(window);
# endif
#endif
    /* SDL_Quit(); is called by atexit() */
}

/*
 * Flip between windowed and full-screen mode in response to the Ctrl-F key.
 * "fullscreen" in ui.c knows whether we are in fullscreen mode already or not.
 */
void
gui_fullscreen()
{
#if EVAS_VIDEO
    fullscreen = !fullscreen;
    ecore_evas_fullscreen_set(ee, fullscreen);
#elif SDL_VIDEO
    gui_deinit();
    fullscreen = !fullscreen;
    gui_init(NULL);	/* Reuse the same file name as title */
    draw_axes();
    repaint_display(FALSE);
#endif
}

void
gui_h_scroll_by(int scroll_by)
{
    if (scroll_by > 0) {
	/* Usual case: scrolling the display left to advance in time */
#if EVAS_VIDEO
	int y;
	for (y=min_y; y <= max_y; y++) {
	    memmove(imagedata+y*imagestride + (4 * min_x),
		    imagedata+y*imagestride + (4 * min_x) + (4 * scroll_by),
		    4 * (max_x - min_x + 1) - 4 * scroll_by);
	}
#elif SDL_VIDEO
	{
	    SDL_Rect from, to;
	    int err;

	    from.x = min_x + scroll_by; to.x = min_x;
	    from.y = to.y = (disp_height-1-max_y);
	    from.w = max_x - min_x + 1 - scroll_by;    /* to.[wh] are ignored */
	    from.h = max_y - min_y + 1;

	    if ((err = SDL_BlitSurface(screen, &from, screen, &to)) != 0) {
		fprintf(stderr, "SDL Blit failed with value %d.\n", err);
	    }
	}
#endif
    }

    if (scroll_by < 0) {
	/* Happens when they seek back in time */
#if EVAS_VIDEO
	int y;
	for (y=min_y; y <= max_y; y++)
	    memmove(imagedata+y*imagestride + (4 * min_x) + (4 * -scroll_by),
		    imagedata+y*imagestride + (4 * min_x),
		    4 * (max_x - min_x + 1) - (4 * -scroll_by));
#elif SDL_VIDEO
	{
	    SDL_Rect from, to;
	    int err;

	    from.x = min_x; to.x = min_x -scroll_by;
	    from.y = to.y = (disp_height-1-max_y);
	    from.w = max_x - min_x + 1 - -scroll_by;  /* to.[wh] are ignored */
	    from.h = max_y - min_y + 1;

	    if ((err = SDL_BlitSurface(screen, &from, screen, &to)) != 0) {
		fprintf(stderr, "SDL Blit failed with value %d.\n", err);
	    }
	}
#endif
    }
}

/* Scroll the graph vertically by a number of pixels.
 * A positive value of scroll_by means to move to higher frequencies by
 * moving the graphic data downwards; a negative value to lower frequencies
 * by moving the displayed data upward.
 */
void
gui_v_scroll_by(int scroll_by)
{
    if (scroll_by > 0) {
	/* Move to higher frequencies by scrolling the graphic down */
#if EVAS_VIDEO
	int y;	/* destination y coordinate */
	/* Copy lines downwards, i.e. forwards in memory and
	 * start from the bottom and work upwards */
	for (y=min_y; y <= max_y-scroll_by; y++)
	    memmove(imagedata + 4*min_x + imagestride*(disp_height-1-y),
		    imagedata + 4*min_x + imagestride*(disp_height-1-y-scroll_by),
		    4 * (max_x - min_x + 1));
#elif SDL_VIDEO
	{
	    SDL_Rect from, to;
	    int err;

	    from.x = min_x; to.x = min_x;
	    from.w = max_x - min_x + 1;

	    /* Invert coords because SDL is 0-to-top */
	    from.y = (disp_height - 1) - max_y;
	    to.y   = (disp_height - 1) - (max_y - scroll_by);
	    from.h = (max_y - min_y + 1) - scroll_by;

	    if ((err = SDL_BlitSurface(screen, &from, screen, &to)) != 0) {
		fprintf(stderr, "SDL Blit failed with value %d.\n", err);
	    }
	}
#endif
    }

    if (scroll_by < 0) {
	/* Move to lower frequencies by scrolling the graphic up */
#if EVAS_VIDEO
	int y;	/* destination y coordinate of the copy */
	/* Copy lines upwards, i.e. to lower memory */
	for (y=max_y; y >= min_y-scroll_by; y--)
	    memmove(imagedata + 4*min_x + imagestride*(disp_height-1-y),
		    imagedata + 4*min_x + imagestride*(disp_height-1-y-scroll_by),
		    4 * (max_x - min_x + 1));
#elif SDL_VIDEO
	{
	    SDL_Rect from, to;
	    int err;

	    from.x = min_x; to.x = min_x;
	    from.y = (disp_height-1) - max_y - scroll_by;
	    to.y   = (disp_height-1) - max_y;
	    from.w = max_x - min_x + 1;    /* to.[wh] are ignored */
	    from.h = max_y - min_y + 1 - -scroll_by;

	    if ((err = SDL_BlitSurface(screen, &from, screen, &to)) != 0) {
		fprintf(stderr, "SDL Blit failed with value %d.\n", err);
	    }
	}
#endif
    }
}

/* Fill a rectangle with a single colour" */
void
gui_paint_rect(int from_x, int from_y, int to_x, int to_y, color_t color)
{
#if EVAS_VIDEO
    unsigned char *p;	/* pointer to pixel to set */
    int y, x;

    /* Paint top to bottom so that we move forward in the imagedata */
    for (y=to_y,
	 p = (unsigned char *)((unsigned int *)imagedata)
				+ (disp_height-1-to_y) * imagestride;
	 y >= from_y;
	 y--, p += imagestride)
	    for (x=from_x; x <= to_x; x++)
		((unsigned int *)p)[x] = color;
#elif SDL_VIDEO
    SDL_Rect rect = {
	from_x, (disp_height-1)-to_y, /* SDL is 0-at-top, we are 0-at-bottom */
	to_x - from_x + 1, to_y - from_y + 1
    };

    SDL_FillRect(screen, &rect, color);
#endif
}

/* Fill a pixel column with a single colour, probably "green" or "background" */
void
gui_paint_column(int pos_x, int from_y, int to_y, unsigned int color)
{
    /* gui_paint_column is only called for the display area */
    if (pos_x >= min_x && pos_x <= max_x)
	gui_paint_rect(pos_x, from_y, pos_x, to_y, color);
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

/* Convert a 0-255 value of red, green and blue to a color_t */
color_t
RGB_to_color(primary_t red, primary_t green, primary_t blue)
{
#if EVAS_VIDEO
    return blue | (green << 8) | (red << 16) | 0xFF000000;
#elif SDL_VIDEO
    return SDL_MapRGB(screen->format, red, green, blue);
#endif
}

/* Calls to this should be bracketed by gui_lock() and gui_unlock(). */
void
gui_putpixel(int x, int y, color_t color)
{
#if EVAS_VIDEO
    color_t *row;	/* of pixels */
# endif

    if (x < 0 || x >= disp_width ||
	y < 0 || y >= disp_height) return;

#if EVAS_VIDEO
    row = (color_t *)&imagedata[imagestride * ((disp_height-1) - y)];
    row[x] = color;
#elif SDL_VIDEO

/* Macro derived from http://sdl.beuc.net/sdl.wiki/Pixel_Access's putpixel() */
#define putpixel(screen, x, y, pixel) \
	((Uint32 *)((Uint8 *)screen->pixels + (y) * screen->pitch))[x] = pixel

    /* SDL has y=0 at top */
    putpixel(screen, x, (disp_height-1) - y, color);
# endif
}

/* Dump the current screen contents to a PNG file.
 *
 * Returns TRUE on success, FALSE on failure
 */
bool
gui_output_png_file(const char *filename)
{
    png_bytepp  png_rows = Malloc(disp_height * sizeof(*png_rows));
    png_structp png      = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0,0);
    png_infop   png_info = png_create_info_struct(png);
    FILE *file = fopen(filename, "wb");
    int y;

    if (file == NULL) {
      fprintf(stderr, "Can't open \"%s\": %s\n", filename, strerror(errno));
      return FALSE;
    }

    if (png == NULL || png_info == NULL) {
    	fprintf(stderr, "Can't create PNG write structure.\n");
	return FALSE;
    }

    green_line_off = TRUE;
    repaint_display(TRUE);	/* repainting all reflects a changed logmax */

    png_init_io(png, file);
    png_set_IHDR(png, png_info,
    		 (png_uint_32)disp_width, (png_uint_32)disp_height,
		 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_ADAM7,
      		 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    for (y = 0; y < disp_height; y++) {
	png_bytep row = (png_bytep)
#if EVAS_VIDEO
	    (&imagedata[imagestride * y]);
#elif SDL_VIDEO
	    ((Uint8 *)screen->pixels + (y) * screen->pitch);
	{
	    int x;
	    for (x=0; x < disp_width; x++)
		((unsigned long *)row)[x] |= 0xFF000000; /* Set alpha to 100% */
	}
#endif
	png_rows[y] = row;
    }

    png_set_rows(png, png_info, png_rows);
    png_write_png(png, png_info, PNG_TRANSFORM_BGR, NULL);
    png_destroy_write_struct(&png, &png_info);
    free(png_rows);
    fclose(file);

    /* If just outputting a PNG and quitting (-o), no need to restore display */
    if (!output_file) {
	green_line_off = FALSE;
	repaint_column(disp_offset, min_y, max_y, TRUE);
	gui_update_column(disp_offset);
    }

    printf("Dumped the window to %s\n", filename);

    return TRUE;
}
