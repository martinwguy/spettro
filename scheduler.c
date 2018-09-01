/*
 * scheduler.c - Maintain a list of columns to be refreshed with the same
 * number of FFT calculation threads as there are CPUs, not zillions of
 * threads as before.
 *
 * The scheduler runs in its own thread; when main.c asks for a column to be
 * refreshed, it adds an item to the list of FFTs to perform.
 * When an FFT thread wants work, it asks the scheduler for an FFT to
 * perform, the scheduler removes a calc_t structure from the list of
 * pending column refreshes, kept in time order, and hands it to the
 * FFT thread that asked for it. The FFT thread fetches the appropriate
 * audio fragment, applies he window function, performs the FFT and
 * returns the result to the notify_cb() callback, which refreshes the
 * appropriate screen column applying the color map and logarithmic frequency
 * axis distortion.
 */

/* The list of FFTs to do consists of single-column refreshes, not a range of
 * columns as before. This improves multi-CPU rendition bcos with ranges, a
 * single CPU would calculate the initial display instead of all of them.
 * The list is kept in time order since the beginning of the piece.
 *
 * If they, say, zoom in in the time direction, scheduling lots of requests,
 * or pan vigorously through the piece, how can we eliminate the
 * no-longer-required column refreshes from the list? Presumably, the
 * scheduler does it when it selects the next job from the list, filtering
 * the calculations according to the current disp_time, step time zoom and
 * window width.
 * If they do a lot of panning or zooming in and out, the list will fill
 * with column recalculations that are always rejected by the scheduler as
 * being out of range or not corresponding to a current pixel column.
 * We'll need to weed it periodically, or when a zoom-out or a pan happens.
 */

#include "config.h"
#include "spettro.h"
#include "calc.h"
#include "lock.h"
#include "main.h"
#include "scheduler.h"

#if 0
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) do{}while(0)
#endif

#include <malloc.h>		/* for free(!) */
#include <math.h>		/* for floor() */

#if ECORE_MAIN
#include <Ecore.h>
#elif SDL_MAIN
#include <unistd.h>		/* for sysconf() */
#include <errno.h>
#include <string.h>		/* for strerror() */
#include <pthread.h>
#endif

static void print_list(void);

/* The list of moments to calculate */
static calc_t *list = NULL;

/* Launch the scheduler. Returns TRUE on success.
 * It turns out we don't need a scheduler thread; the FFT threads just call
 * get_work() repeatedly.
 *
 * "nthreads" says how many FFT threads to start; 0 means the same number
 * as there are CPUs.
 */
void
start_scheduler(int nthreads)
{
#if ECORE_MAIN
    if (nthreads == 0) nthreads = ecore_thread_max_get();

    /* Start the FFT calculation threads, which ask get_work() for work */
    while (nthreads-- > 0) {
	if (ecore_thread_feedback_run(calc_heavy, calc_notify,
				      NULL, NULL, NULL, EINA_FALSE) == NULL) {
	    fprintf(stderr, "Can't start FFT-calculating thread.\n");
	    exit(1);
	}
    }
#elif SDL_MAIN
    {
	pthread_attr_t attr;

	if (pthread_attr_init(&attr) != 0 ||
	    pthread_attr_setdetachstate(&attr,
					PTHREAD_CREATE_DETACHED) != 0) {
	    fprintf(stderr, "Cannot set pthread attributes. Continuing anyway...\n");
	}

	/* Start the FFT threads */
	if (nthreads == 0) nthreads = sysconf(_SC_NPROCESSORS_ONLN);
	while (nthreads-- > 0) {
	    pthread_t thread;
	    if (pthread_create(&thread, &attr, calc_heavy, NULL) != 0) {
		fprintf(stderr, "Cannot create a calculation thread: %s\n",
			strerror(errno));
		return;
	    }
	}

	pthread_attr_destroy(&attr);
    }
#endif
}

/* Ask for a range of FFTs to be queued for execution */
void
schedule(calc_t *calc)
{
    /* Add it to the list in time order */
    calc_t **cpp;	/* Pointer to the "next" field of the previous cell */

    lock_list();

DEBUG("Scheduling %g-%g... ", calc->from, calc->to);
    if (list == NULL) {
DEBUG("Adding to empty list:\n");
	list = calc;
	calc->next = calc->prev = NULL;
	print_list();
	unlock_list();
	return;
    }

    for (cpp = &list;
	 *cpp != NULL && (*cpp)->from < calc->from - DELTA;
	 cpp = &((*cpp)->next))
	;

    /* When *cpp's time is greater than calc's we need to insert the new
     * calc_t before the later one. If we find one with the same time,
     * replace it.
     */
    if (*cpp == NULL) {
	/* At end of list. Just add it to the end. */
DEBUG("Adding at end of list\n");
	calc->next = NULL;
	/* For the prev pointer, cpp points at its "next" item */
	calc->prev = (calc_t *)((char *)cpp - ((char *)&(calc->next)-(char *)calc));
	*cpp = calc;
    } else /* If a duplicate in time, replace the existing one */
    if ((*cpp)->from > calc->from - DELTA &&
        (*cpp)->from < calc->from + DELTA) {
DEBUG("Replacing existing item\n");
	    calc_t *old = *cpp;
	    calc->next = old->next;
	    calc->prev = old->prev;
	    if (calc->next) calc->next->prev = calc;
	    if (calc->prev) calc->prev->next = calc;
	    if (list == old) list = calc;
	    free(old);
    } else {
DEBUG("Adding before later item\n");
	calc_t *cp = *cpp; /* The item to add it before */

	/* Add it before the one that is later than it. */
	calc->next = cp;
	calc->prev = cp->prev;
	if (calc->prev) calc->prev->next = calc;
	else list = calc;
	cp->prev = calc;
    }

    print_list();
    unlock_list();
}

/* The FFT threads ask here for the next FFT to perform
 *
 * Priority: 1. times corrisponding to columns visible in the display window:
 *		first those immediately ahead of the play position,
 *		in time order,
 *	     2. then, any missing columns left of the play position
 *		in front-to-back order.
 */
calc_t *
get_work()
{
    /* Again, we will need to remove the most interesting element from the list
     * so again we keep a pointer to the "next" field of the previous cell.
     */
    calc_t **cpp;

    lock_list();

DEBUG("Getting work... ");

    if (list == NULL) {
DEBUG("List is empty\r");
	unlock_list();
	return NULL;
    }

    /* First, drop any list items that are off the left side of the screen */
    while (list != NULL && list->to < disp_time - disp_offset*step - DELTA) {
	calc_t *old_cp = list;
	old_cp = list;	/* Remember cell to free */
	list = list->next;
	/* New first cell , if any, has no previous one */
	if (list != NULL) list->prev = NULL;
	free(old_cp);
    }

    if (list == NULL) {
DEBUG("List is empty after dropping before-screens\r");
	unlock_list();
	return NULL;
    }

    /* Search the list to find the first calculation that is >= disp_time.
     * We include disp_time itself because it will need to be repainted
     * as soon as the next scroll happens, so having it ready is preferable.
     */
    for (cpp = &list; (*cpp) != NULL; cpp = &((*cpp)->next)) {
	if ((*cpp)->from >= disp_time - DELTA) {
	    /* Found the first time >= disp_time */
	    break;
	}
    }
    /* If the first one >= disp_time is off the right side of the screen,
     * remove it and anything after it */
    if (*cpp != NULL &&
	(*cpp)->from > disp_time + (disp_width-1-disp_offset)*step + DELTA) {
	calc_t *cp = *cpp;	/* List pointer to free unwanted cells */
	while (cp != NULL) {
	    calc_t *old_cp = cp;
	    cp = cp->next;
	    free(old_cp);
	}
	*cpp = NULL;
    }

    if (list == NULL) {
DEBUG("List is empty after dropping after-screens\r");
	unlock_list();
	return NULL;
    }

    if (*cpp != NULL) {
	/* We have a column after disp_time that's on-screen so
	 * remove this calc_t from the list and hand it to the
	 * hungry calculation thread. */
	calc_t *cp = (*cpp);	/* Proto return value, the cell we detach */

DEBUG("Picked from %g to %g from list\n", cp->from, cp->to);

	*cpp = cp->next;
	if (cp->next) cp->next->prev = cp->prev;
	cp->next = cp->prev = NULL; /* Not strictly necessary but */
	print_list();
	unlock_list();
	return(cp);
    }

    if (*cpp == NULL & cpp != &list) {
	/* We got to the end of the list and all work is <= disp_time */
	calc_t *cp;
	static calc_t calc;	/* Used for measuring the structure layout */

	/* Get address of last cell in the list */
	cp = (calc_t *)((char *)cpp - (((char *)&calc.next - (char *)&calc)));

DEBUG("Last cell is from %g to %g\n", cp->from, cp->to);

	/* Remove the last element and tell FFT to calculate it */
	if (cp->prev == NULL) list = NULL;
	else cp->prev->next = NULL;
	cp->prev = cp->next = NULL;	/* Not necessary but */
	print_list();

	unlock_list();
	return cp;
    }
DEBUG("List is empty after all\r");

    unlock_list();
    return NULL;
}

/* When they zoom out on the frequency axis, we need to remove all the
 * scheduled calculations that no longer correspond to a pixel column.
 */
void
reschedule_for_bigger_step()
{
    calc_t **cpp;

    lock_list();

    for (cpp = &list; *cpp != NULL; /* see below */) {
	/* If its time is no longer a multiple of the step, drop it */
	if ((*cpp)->from > floor((*cpp)->from / step) * step + DELTA) {
	    calc_t *cp = *cpp;	/* Old cell to free */
	    /* Rewrite "next" field of previous cell or the "list" pointer */
	    *cpp = cp->next;
	    /* and the "prev" field of the following cell, if there is one */
	    if (cp->next) cp->next->prev = cp->prev;
	    free(cp);
	    /* and *cpp is already the next cell to examine */
	} else {
	    cpp = &((*cpp)->next);	/* loop reinitialization */
	}
    }

    unlock_list();
}

static void
print_list()
{
    calc_t *cp;

DEBUG("List:");
    for (cp = list; cp != NULL; cp=cp->next)
	DEBUG(" %c%g-%g%c", 
		cp->prev ? (cp->prev == &list ? 'L' : '<') : '.',
		cp->from, cp->to,
		cp->next ? '>' : '.');
DEBUG("\n");
}
