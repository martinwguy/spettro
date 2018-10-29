/*
 * timer.c - The timer is used to keep scrolling the display
 */

#include "spettro.h"
#include "timer.h"
#include "main.h"
#include "gui.h"

#include <math.h>

/* The timer and its callback function. */
#if ECORE_TIMER

#include <Ecore.h>
#include <Evas.h>
#define NO_TIMER NULL
extern Evas_Object *em;	/* From main.c */
static Ecore_Timer *timer = NULL;
static Eina_Bool timer_cb(void *data);	/* The timer callback function */
static int scroll_event;   /* Our user-defined event to activate scrolling */
static Eina_Bool scroll_cb(void *data, int type, void *event);

#elif SDL_TIMER

# include <SDL.h>
# if SDL1
#  define NO_TIMER NULL
# elif SDL2
#  define NO_TIMER 0
# endif
static SDL_TimerID timer = NO_TIMER;
static Uint32 timer_cb(Uint32 interval, void *data);

#else
# error "Define ECORE_TIMER or SDL_TIMER"
#endif

void
start_timer()
{
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
    if (timer == NO_TIMER) {
	fprintf(stderr, "Couldn't initialize scrolling timer for step of %g secs.\n", step);
	exit(1);
    }
}

void
stop_timer()
{
#if ECORE_MAIN
    (void) ecore_timer_del(timer);
#elif SDL_MAIN
    SDL_RemoveTimer(timer);
#endif
}

void
change_timer_interval(double interval)
{
#if ECORE_TIMER
    if (ecore_timer_del(timer) == NULL ||
	(timer = ecore_timer_add(interval, timer_cb, (void *)em)) == NULL) {
#elif SDL_TIMER
    if (!SDL_RemoveTimer(timer) ||
	(timer = SDL_AddTimer((Uint32)lrint(interval * 1000), timer_cb, NULL)) == NO_TIMER) {
#endif
	fprintf(stderr, "Couldn't change rate of scrolling timer.\n");
	exit(1);
    }
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
