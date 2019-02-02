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
 * The list can contain work that is no longer relevant, either because the
 * columns are no longer on-screen or because the calculation parameters
 * (speclen, window_function) have changed since it was scheduled.
 * We remove these while searching for new work in get_work().
 */

#include "spettro.h"
#include "scheduler.h"

#include "audio.h"
#include "cache.h"
#include "calc.h"
#include "gui.h"
#include "lock.h"
#include "paint.h"
#include "ui.h"

/* How many threads are busy calculating an FFT for us? */
int jobs_in_flight = 0;

#if 0
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) do{}while(0)
#endif

#if SDL_MAIN
#include <SDL.h>
#endif


#if ECORE_MAIN
#include <unistd.h>		/* for usleep() */
#include <Ecore.h>
#elif SDL_MAIN
#include <unistd.h>		/* for sysconf() */
#include <errno.h>
#include <string.h>		/* for strerror() */
#include <pthread.h>
#endif

#if SDL_MAIN
bool sdl_quit_threads = FALSE;	/* When true, calc threads should return */
#endif

static void print_list(void);

/* The list of moments to calculate */
static calc_t *list = NULL;

/* start_scheduler(): Launch the scheduler. Returns TRUE on success.
 *
 * It turns out we don't need a separate thread for the scheduler:
 * the FFT threads just call get_work() repeatedly.
 *
 * "nthreads" says how many FFT threads to start; 0 means the same number
 * as there are CPUs.
 */

static int threads = 0;		/* The number of threads we have started */
#if ECORE_MAIN
static Ecore_Thread **thread;	/* Array of threads */
#elif SDL_MAIN
static SDL_Thread **thread;	/* Array of threads */
#endif

#if ECORE_MAIN
static void ecore_calc_notify(void *data, Ecore_Thread *thread, void *msg_data);
static void ecore_calc_heavy(void *data, Ecore_Thread *thread);
#elif SDL_MAIN
static int sdl_calc_heavy(void *data);
#endif

void
start_scheduler(int nthreads)
{
#if ECORE_MAIN
    if (nthreads == 0) nthreads = ecore_thread_max_get();

    thread = (Ecore_Thread **) Malloc(nthreads * sizeof(*thread));

    /* Start the FFT calculation threads, which ask get_work() for work. */
    /* try_no_queue==TRUE so that all threads run simultaneously. */
    for (threads=0; threads < nthreads; threads++) {
	thread[threads] = ecore_thread_feedback_run(
				ecore_calc_heavy, ecore_calc_notify,
				NULL, NULL, NULL, EINA_TRUE);
	if (thread[threads] == NULL) {
	    fprintf(stderr, "Can't start an FFT-calculating thread.\n");
	    if (threads == 0) {
		/* Can't start the first thread: fatal */
		exit(1);
	    } else {
		/* There's at least one thread, so carry on with that */
		threads++;
		break;
	    }
	}
    }
#elif SDL_MAIN
    {
	if (nthreads == 0) nthreads = sysconf(_SC_NPROCESSORS_ONLN);

	thread = (SDL_Thread **) Malloc(nthreads * sizeof(*thread));

	/* Start the FFT threads */
	for (threads=0; threads < nthreads; threads++) {
	    char name[16];
	    sprintf(name, "calc%d", threads);
	    thread[threads] = SDL_CreateThread(sdl_calc_heavy, NULL
#if SDL2
								   , NULL
#endif
									 );
	    if (thread[threads] == NULL) {
		fprintf(stderr, "Cannot create a calculation thread: %s\n",
			SDL_GetError());
		if (threads == 0) {
		    /* Can't start the first thread: fatal */
		    exit(1);
		}
		return;
	    }
	}
    }
#endif
}

/* The function called as the body of the FFT-calculation threads.
 *
 * Get work from the scheduler, do it, call the result callback and repeat.
 * If get_work() returns NULL, there is nothing to do, so sleep a little.
 */
#if ECORE_MAIN
static void
ecore_calc_heavy(void *data, Ecore_Thread *thread)
{
    /* Loop until this thread is scheduled to be cancelled */
    while (ecore_thread_check(thread) == FALSE) {
	calc_t *work = get_work();
	if (work == NULL) {
	    usleep((useconds_t)100000); /* Sleep for a tenth of a second */
	} else {
	    work->thread = thread;
	    calc(work);
	}
    }
}
#elif SDL_MAIN
static int
sdl_calc_heavy(void *data)
{
    calc_t *work;

    work = get_work();
    while (!sdl_quit_threads) {
	if (work == NULL) {
	    usleep((useconds_t)100000); /* No work: sleep for a tenth of a second */
	} else {
	    calc(work);
	}
        work = get_work();
    }
    return 0;	/* Make GCC -Wall happy */
}
#endif

#if ECORE_MAIN
static void
ecore_calc_notify(void *data, Ecore_Thread *thread, void *msg_data)
{
    calc_notify((result_t *) msg_data);
}
#endif

void
stop_scheduler(void)
{
    int n;	/* loop variable */

#if ECORE_MAIN

    int active = 0;	/* How many threads haven't stopped? */

    /* Tell Ecore to cancel the thread when it can */
    for (n=0; n < threads; n++) {
	if (ecore_thread_cancel(thread[n]))
	    thread[n] = NULL;
	else
	    active++;
    }
    /* Wait for the threads to die */
    while ((active = ecore_thread_active_get()) > 0) {
	usleep(100000);
    }

#elif SDL_MAIN

    sdl_quit_threads = TRUE;
    for (n=0; n < threads; n++) {
    	SDL_WaitThread(thread[n], NULL);
    }

#endif
    free(thread);
    threads = 0;
}

/* Ask for an FFT to be queued for execution */
void
schedule(calc_t *calc)
{
    /* Add it to the list in time order */
    calc_t **cpp;	/* Pointer to the "next" field of the previous cell */

    if (recall_result(calc->t, calc->speclen, calc->window)) {
	fprintf(stderr, "scheduler drops calculation already in cache for time %g\n",
		calc->t);
	return;
    }

    lock_list();

DEBUG("Scheduling %g/%d/%c... ", calc->t, calc->speclen,
      window_key(calc->window));
    if (list == NULL) {
DEBUG("Adding to empty list:\n");
	list = calc;
	calc->next = calc->prev = NULL;
	print_list();
	unlock_list();
	return;
    }

    for (cpp = &list;
	 *cpp != NULL && DELTA_LT((*cpp)->t, calc->t);
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
    if (DELTA_EQ((*cpp)->t, calc->t)) {
DEBUG("Replacing existing item/%d/%c at %g with new/%d/%c\n",
      (*cpp)->speclen, (*cpp)->t, window_key((*cpp)->window),
      calc->speclen, window_key((*cpp)->window));
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

/*
 * When they change the FFT size or the window function, forget all work
 * scheduled for the old ones.
 * Any results from running FFT threads for the old size will be filtered
 * by calc_notify().
 */
void
drop_all_work()
{
    calc_t *cp;

    lock_list();
    cp = list;
    while (cp != NULL) {
	calc_t *new_cp = cp->next;
	free(cp);
	cp = new_cp;
    }
    list = NULL;
    unlock_list();
}

/* Is there any work still queued to be done? */
bool
there_is_work()
{
    return list != NULL;
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
    while (list != NULL && DELTA_LT(list->t, disp_time - disp_offset*step)) {
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
	if (DELTA_GE((*cpp)->t, disp_time)) {
	    /* Found the first time >= disp_time */
	    break;
	}
    }
    /* If the first one >= disp_time is off the right side of the screen,
     * remove it and anything after it */
    if (*cpp != NULL &&
	DELTA_GT((*cpp)->t, disp_time + (disp_width-1-disp_offset)*step)) {
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

    while (*cpp != NULL) {
	/* We have a column after disp_time that's on-screen so
	 * remove this calc_t from the list and hand it to the
	 * hungry calculation thread. */
	calc_t *cp = (*cpp);	/* Proto return value, the cell we detach */

	/* If speclen has changed since the work was scheduled,
	 * drop this calc and continue searching. This never happens,
	 * I guess because of calls to drop_all_work() when the params change.
	 */
	if (cp->speclen != speclen || cp->window != window_function) {
	    calc_t *cp = *cpp;
	    if (cp->next) cp->next->prev = cp->prev;
	    if (cp->prev) cp->prev->next = cp->next;
	    else list = cp->next;
	    free(cp);
fprintf(stderr, "Avanti!\n");
	    continue;
	}

DEBUG("Picked %g/%d/%c from list\n", cp->t, cp->speclen,
      window_key(cp->window));

	*cpp = cp->next;
	if (cp->next) cp->next->prev = cp->prev;
	if (cp->prev) cp->prev->next = cp->next;
	cp->next = cp->prev = NULL; /* Not strictly necessary but */
	jobs_in_flight++;
	print_list();
	unlock_list();
	return cp;
    }

    if (*cpp == NULL && cpp != &list) {
	/* We got to the end of the list and all work is <= disp_time */
	calc_t *cp;
	static calc_t calc;	/* Used for measuring the structure layout */

	/* Get address of pointer to last cell in the list */
	cp = (calc_t *)((char *)cpp - (((char *)&calc.next - (char *)&calc)));

DEBUG("Last cell is at time %g\n", cp->t);

	/* Remove the last element and tell FFT to calculate it */
	if (cp->prev == NULL) list = NULL;
	else cp->prev->next = NULL;
	cp->prev = cp->next = NULL;	/* Not necessary but */
	jobs_in_flight++;
	print_list();
	unlock_list();

	if (cp->speclen != speclen || cp->window != window_function) {
	    calc_t *cp = *cpp;
fprintf(stderr, "Dropping work at %g/%d/%c for wrong parameters\n", 
	cp->t, cp->speclen, window_key(cp->window));
	    if (cp->next) cp->next->prev = cp->prev;
	    if (cp->prev) cp->prev->next = cp->next;
	    else list = cp->next;
	    free(cp);
	    return NULL;	/* Should continue searching really */
	}
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
	if (DELTA_GT((*cpp)->t, floor((*cpp)->t / step) * step)) {
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
    for (cp = list; cp != NULL; cp=cp->next) {
	DEBUG(" %g/%d", cp->t, cp->speclen);
	if (cp->window != window_function)
	    DEBUG("/%c", window_(cp->window));
    }
DEBUG("\n");
}

/*
 * The main loop has been notified of the arrival of a result. Process it.
 */

extern char *output_file;	/* In main.c */

void
calc_notify(result_t *result)
{
    int pos_x;	/* Where would this column appear in the displayed region? */

    result = remember_result(result);

    if (result->speclen != speclen || result->window != window_function) {
	/* This is the result from an old call to schedule() before
	 * the parameters changed.
	 * Keep it in the cache in case they flip back to old parameters
	 */
	return;
    }

    /* What screen coordinate does this result correspond to? */
    pos_x = lrint(disp_offset + (result->t - disp_time) * ppsec);

    /* Update the display if the column is in the displayed region
     * and isn't at the green line's position
     */
    if (pos_x >= min_x && pos_x <= max_x) {
	paint_column(pos_x, min_y, max_y, result);
	gui_update_column(pos_x);
    }

    jobs_in_flight--;

    /* We can output the PNG file for the -o option when all work is complete */

    if (output_file != NULL && jobs_in_flight == 0 && !there_is_work()) {
	gui_output_png_file(output_file);
	gui_quit_main_loop();
	return;
    }

    /* To avoid an embarassing pause at the start of the graphics, we wait
     * until the FFT delivers its first result before starting the player.
     */
    if (autoplay) {
	start_playing();
	autoplay = FALSE;
    }
}
