/*
 * timer.c - The timer is used to keep scrolling the display
 */

#include "spettro.h"
#include "timer.h"
#include "gui.h"
#include "paint.h"		/* for do_scroll() */

/* The timer and its callback function. */
#if ECORE_TIMER

#include <Ecore.h>
#include <Evas.h>
typedef Ecore_Timer * timer_type;
#define NO_TIMER NULL

extern Evas_Object *em;	/* From main.c */

static Eina_Bool timer_cb(void *data);	/* The timer callback function */
static int scroll_event;   /* Our user-defined event to activate scrolling */
static Eina_Bool scroll_cb(void *data, int type, void *event);

#elif SDL_TIMER

# include <SDL.h>

# if SDL1
typedef SDL_TimerID timer_type;
#  define NO_TIMER NULL
# elif SDL2
typedef Uint32 timer_type;
#  define NO_TIMER 0
# endif
static Uint32 timer_cb(Uint32 interval, void *data);

#else
# error "Define ECORE_TIMER or SDL_TIMER"
#endif

#if EVAS_VIDEO
# include <Ecore_X.h>
#endif

static timer_type timer = NO_TIMER;

/* Implementation-specific code used by the public functions */

static void
add_timer(double interval)
{
    /* We limit the scrolling rate to some minimum to avoid GUI death
     * at microscopic intervals */
    double minimum_interval = 1/50.0;	/* typical CRT frame rate */

    /* If we can find out the monitor's refresh rate, use that instead */
#if EVAS_VIDEO
    /* How do you ask Ecore whether it's running on X or not?
     * If you call ecore_x_window_focus_get() without X, Ecore segfaults.
     */
    if (getenv("DISPLAY") != NULL) {
	Ecore_X_Randr_Refresh_Rate X_refresh_rate;	/* == short */
	Ecore_X_Window X = ecore_x_window_focus_get();	/* == uint */

	/* Ecore_X_Randr_Refresh_Rate==short */
	X_refresh_rate =
	    ecore_x_randr_screen_primary_output_current_refresh_rate_get(X);

        minimum_interval = 1.0 / X_refresh_rate;
    }
#endif

    if (interval < minimum_interval) interval = minimum_interval;

#if ECORE_TIMER
    /* The timer callback just generates an event, which is processed in
     * the main ecore event loop to do the scrolling in the main loop
     */
    scroll_event = ecore_event_type_new();
    ecore_event_handler_add(scroll_event, scroll_cb, NULL);
    timer = ecore_timer_add(interval, timer_cb, (void *)em);
#elif SDL_TIMER
    timer = SDL_AddTimer((Uint32)lrint(interval * 1000), timer_cb, (void *)NULL);
#endif
    if (timer == NO_TIMER) {
	fprintf(stderr, "Couldn't add a timer for an interval of %g secs.\n", interval);
	exit(1);
    }
}

static void
delete_timer()
{
#if ECORE_MAIN
    (void) ecore_timer_del(timer);
#elif SDL_MAIN
    SDL_RemoveTimer(timer);
#endif
}

/* Public functions */

extern double fps;	/* From main.c */

void
start_timer()
{
    /* Start screen-updating and scrolling timer */
    add_timer(1.0/fps);
}

void
stop_timer()
{
    delete_timer();
}

void
change_timer_interval(double interval)
{
    delete_timer();
    add_timer(interval);
}

/*
 * The periodic timer callback that, when playing, schedules scrolling of
 * the display by one pixel.
 * When paused, the timer continues to run to update the display in response to
 * seek commands.
 */

/* This is used to ensure that only one scroll event is ever in the queue,
 * otherwise is you're short of CPU, the event queue fills up with
 * unprocessed scroll events and other events (keypresses, results) are lost.
 */
bool scroll_event_pending = FALSE;

/* Implementation-specific code to handle the timer callback */

#if ECORE_TIMER

static Eina_Bool
timer_cb(void *data)
{
    /* Generate a user-defined event which will be processed in the main loop */
    if (!scroll_event_pending) {
	ecore_event_add(scroll_event, NULL, NULL, NULL);
	scroll_event_pending = TRUE;
    }

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

    /* We only want one scroll event pending at a time, otherwise if there's
     * insufficient CPU, the event queue fills up with them and other events
     * stop working too (result events, key presses etc)
     */
    if (!scroll_event_pending) {
	SDL_Event event;

	event.type = SDL_USEREVENT;
	event.user.code = SCROLL_EVENT;
	if (SDL_PushEvent(&event) != SDL_PUSHEVENT_SUCCESS) {
	    fprintf(stderr, "Couldn't push an SDL scroll event\n");
	} else {
	    scroll_event_pending = TRUE;
	}
    }

    return(interval);
}

#endif
