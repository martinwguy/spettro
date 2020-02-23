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
 * scheduler.c - Maintain a list of columns to be refreshed, probably
 * using the same number of FFT calculation threads as there are CPUs.
 *
 * The main code calls start_scheduler() initially, then calls schedule()
 * to ask for a FFT to be done,
 * The FFT threads call get_work() repeatedly, perform the FFT and send an
 * event to * tha main loop, and that calls calc_notify() with the new result,
 * and refreshes some column of the display.
 *
 * The list of pending columns to refresh is kept in time order.
 *
 * The list can contain work that is no longer relevant, either because the
 * columns are no longer on-screen or because the calculation parameters
 * (fft_freq, window_function) have changed since it was scheduled.
 * We remove these while searching for new work in get_work().
 */

#include "spettro.h"
#include "scheduler.h"

#include "audio.h"
#include "cache.h"
#include "calc.h"
#include "convert.h"
#include "gui.h"
#include "lock.h"
#include "paint.h"
#include "ui.h"

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

static void print_list(calc_t *list);

/* The list of moments to calculate */
static calc_t *list = NULL;
/* The list of moments that are currently being calculated */
static calc_t *jobs = NULL;
/* How many threads are busy calculating an FFT for us? */
int jobs_in_flight = 0;

static bool is_in_list(calc_t *calc, calc_t *l);

/* Statics for scheduler */

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

/* start_scheduler(): Start the FFT calculation threads and
 *		      initialize anything the scheduler needs.
 *
 * nthreads: How many FFT threads to start;
 *	     0 means the same number as there are CPUs.
 *
 * Returns TRUE on success.
 */
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
    int speclen = fft_freq_to_speclen(calc->fft_freq,
    				      calc->audio_file->sample_rate);

    /* Is this column's calculation already scheduled or already being performed?
     * This happens a lot, when several scrolls happen before the newly
     * revealed columns' results have come back from the calc threads.
     * Being in flight is four times more common that being already scheduled,
     * so check that first (its list is also shorter).
     */

    if (is_in_list(calc, jobs) || is_in_list(calc, list)) {
	free(calc);
	return;
    }

    /* Do we already have a result for this calculation in the cache? */
    if (recall_result(calc->t, speclen, calc->window)) {
	fprintf(stderr, "scheduler drops calculation already in cache for %g/%g/%c\n",
		calc->t, calc->fft_freq, window_key(calc->window));
	free(calc);
	return;
    }

    lock_list();

DEBUG("Scheduling %g/%g/%c... ", calc->t, calc->fft_freq,
      window_key(calc->window));
    if (list == NULL) {
DEBUG("Adding to empty list:\n");
	list = calc;
	calc->next = NULL;
	print_list(list);
	unlock_list();
	return;
    }

    /* To keep the list in time order, skip over all pending calculations
     * that are earlier than the new one.
     */
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
	*cpp = calc;
    } else /* If a duplicate in time, replace the existing one */
    if (DELTA_EQ((*cpp)->t, calc->t)) {
DEBUG("Replacing existing item %g/%g/%c  with new %g/%g/%c\n",
      (*cpp)->t, (*cpp)->fft_freq, window_key((*cpp)->window),
      (*cpp)->t,   calc->fft_freq, window_key(  calc->window));
	    calc_t *cp = *cpp;
	    calc->next = (*cpp)->next;
	    *cpp = calc;
	    free(cp);
    } else {
DEBUG("Adding before later item\n");
	/* Add it before the one that is later than it.
	 * cpp is pointing to the "next" field of the previous cell.
	 */
	calc->next = *cpp;
	*cpp = calc;
    }

    print_list(list);
    unlock_list();
}

/* Are the results for this calculation already being worked on by
 * one of the calculation threads or in the list of scheduled jobs?
 */
static bool
is_in_list(calc_t *calc, calc_t *l)
{
    calc_t *cp;

    for (cp = l; cp != NULL; cp = cp->next) {
	if (calc->t        == cp->t &&
	    calc->fft_freq == cp->fft_freq &&
	    calc->window   == cp->window) return TRUE;
    }
    return FALSE;
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
 * Give them the earliest one that is on-screen, so that the screen
 * repaints from left to right.
 */
static void put_work_in_flight(calc_t **cpp);

calc_t *
get_work()
{
    /* We will need to remove the most interesting element from the list
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

    /* First, drop any list items that are off the left side of the screen,
     * but keep the same amount of lookahead off the left side of the screen
     * so that the region exposed by scrolling left with <- is precalculated.
     */
    {
	double earliest = screen_column_to_start_time(min_x - LOOKAHEAD);

	while (list != NULL && DELTA_LT(list->t, earliest)) {
	    calc_t *old_cp = list;
	    old_cp = list;	/* Remember cell to free */
	    list = list->next;
	    free(old_cp);
	}
    }

    if (list == NULL) {
DEBUG("List is empty after dropping before-screens\r");
	unlock_list();
	return NULL;
    }

    /* Refresh the screen left-to-right, then the off-screen lookahead */
    cpp = &list;
    while (*cpp != NULL) {
	calc_t *cp = *cpp;	/* Proto return value, the cell we detach */

	/* If the work is not in the area of interest, drop it.
	 * The first condition happens due to look-behind,
	 * the second when scrolling left.
	 */
	if (DELTA_LT(cp->t, screen_column_to_start_time(min_x - LOOKAHEAD)) ||
	    DELTA_GT(cp->t, screen_column_to_start_time(max_x + LOOKAHEAD))) {
	    *cpp = cp->next;
	    free(cp);
	    continue;
	}

	/* If UI settings changed since the work was scheduled,
	 * drop this calc and continue searching. This never happens,
	 * I guess because of calls to drop_all_work() when the params change.
	 */
	if (DELTA_NE(cp->fft_freq, fft_freq) ||
	    cp->window != window_function) {

	    *cpp = cp->next;
	    free(cp);
fprintf(stderr, "Avanti!\n");
	    continue;
	}

	put_work_in_flight(cpp);
	unlock_list();
	return cp;
    }

    /* Then the pre-screen look-behind. */
    cpp = &list;
    while (*cpp != NULL &&
    	   DELTA_LT((*cpp)->t, screen_column_to_start_time(min_x))) {
	calc_t *cp = *cpp;	/* Proto return value, the cell we detach */

	/* If UI settings changed since the work was scheduled,
	 * drop this calc and continue searching.
	 */
	if (DELTA_NE(cp->fft_freq, fft_freq) ||
	    cp->window != window_function) {

	    *cpp = cp->next;
	    free(cp);
fprintf(stderr, "Avanti!\n");
	    continue;
	}

	put_work_in_flight(cpp);
	unlock_list();
	return cp;
    }

DEBUG("List is empty after all\r");

    unlock_list();
    return NULL;
}

/* Convenience function to avoid repetition:
 * Remove this job from the to-do list, put it on the in-flight list
 */
static void
put_work_in_flight(calc_t **cpp)
{
    calc_t *cp = *cpp;

DEBUG("Picked %g/%g/%c from list\n", cp->t, cp->fft_freq,
      window_key(cp->window));

    /* Detach the job from the list of jobs-to-do */
    *cpp = cp->next;

    /* and add it to the list of jobs in flight */
    DEBUG("Adding to "); print_list(jobs);
    cp->next = jobs;
    jobs = cp;
    jobs_in_flight++;

    print_list(list);
}

/* When they zoom out on the frequency axis, we need to remove all the
 * scheduled calculations that no longer correspond to a pixel column.
 */
void
reschedule_for_bigger_secpp()
{
    calc_t **cpp;

    lock_list();

    for (cpp = &list; *cpp != NULL; /* see below */) {
	/* If its time is no longer a multiple of the step, drop it */
	if (DELTA_GT((*cpp)->t, floor((*cpp)->t / secpp) * secpp)) {
	    calc_t *cp = *cpp;	/* Old cell to free */
	    /* Rewrite "next" field of previous cell or the "list" pointer */
	    *cpp = cp->next;
	    free(cp);
	    /* and *cpp is already the next cell to examine */
	} else {
	    cpp = &((*cpp)->next);	/* loop reinitialization */
	}
    }

    unlock_list();
}

static void
print_list(calc_t *l)
{
    calc_t *cp;

DEBUG(l == list ? "List:" : "Jobs:");
if (l == jobs) DEBUG(" [%d]", jobs_in_flight);
    for (cp = l; cp != NULL; cp=cp->next) {
	DEBUG(" %g/%g", cp->t, cp->fft_freq);
	if (cp->window != window_function)
	    DEBUG("/%c", window_key(cp->window));
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

    lock_list();
    /* Remove it from the list of jobs in flight */
    {
	calc_t **cpp;
	DEBUG("Removing from "); print_list(jobs);
	for (cpp = &jobs; *cpp != NULL; cpp = &((*cpp)->next)) {
	    if ((*cpp)->t        == result->t &&
		(*cpp)->fft_freq == result->fft_freq &&
		(*cpp)->window   == result->window) {
		calc_t *cp;

		cp = *cpp;
		*cpp = cp->next;
		jobs_in_flight--;
		goto got_it;
	    }
	}
	fprintf(stderr, "Result for %g/%g/%c is not among the jobs in flight\n",
		result->t, result->fft_freq, window_key(result->window));
    }
got_it:
    unlock_list();

    result = remember_result(result);

    if (result->fft_freq != fft_freq || result->window != window_function) {
	/* This is the result from an old call to schedule() before
	 * the parameters changed.
	 * We don't need to reschedule it because a change in parameters
	 * is always followed by a request to repaint everything.
	 * We keep it in the cache in case they flip back to the old parameters.
	 */
	return;
    }

    /* What screen coordinate does this result correspond to? */
    pos_x = time_to_screen_column(result->t);

    /* Update the display if the column is in the displayed region
     * and isn't at the green line's position
     */
    if (pos_x >= min_x && pos_x <= max_x) {
	paint_column(pos_x, min_y, max_y, result);
	gui_update_column(pos_x);
    }

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
