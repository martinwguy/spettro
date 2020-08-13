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
 * cache.c - Result cache
 *
 * We remember the FFT results we have calculated as calc_t's, in time order,
 * which reflect the parameters that gave rise to that result and the
 * linear magnitude data.
 *
 * The mapping from this to screen coordinates and colors is done on-the-fly
 * at screen update time by paint_column() calling interpolate() and colormap().
 */

#include "spettro.h"
#include "cache.h"

#include "convert.h"
#include "calc.h"
#include "window.h"
#include "ui.h"

#include <string.h>	/* for memcmp() */

static void destroy_result(calc_t *r);

static calc_t *results = NULL; /* Linked list of result structures */
static calc_t *last_result = NULL; /* Last element in the linked list */

/* "result" was obtained from malloc(); it is up to us to free it. */
/* We return the result because, if we find a duplicate in the cache, that
 * becomes the active result.
 */
calc_t *
remember_result(calc_t *result)
{
    /* Drop any stored results more than a screenful before the display */
    double earliest = screen_column_to_start_time(min_x - disp_width);

    while (results != NULL && DELTA_LT(results->t, earliest)) {
	calc_t *r = results;
	results = results->next;
	destroy_result(r);
    }

    /* Now find where to add the result to the time-ordered list */
    if (results == NULL) {
        result->next = NULL;
	results = last_result = result;
    } else {
        /* If it's after the last one (the most common case),
	 * add it at the tail of the list
	 */
	if (DELTA_GT(result->t, last_result->t)) {
            result->next = NULL;
	    last_result->next = result;
	    last_result = result;
	} else {
	    /* If it's before the first one, tack it onto head of list,
	     * otherwise find which element to place it after
	     */
	    calc_t **rp;	/* Pointer to "next" field of previous result
			     * or to "results": the pointer we'll have
			     * to update when we've found the right place.
			     */
	    calc_t *r;	/* Handy pointer to the result to examine */

	    for (rp=&results;
		 (r = *rp) != NULL && DELTA_LE(r->t, result->t);
		 rp = &((*rp)->next)) {
		/* Check for duplicates */
		if (DELTA_EQ(r->t, result->t) &&
		    r->fft_freq == result->fft_freq &&
		    r->window == result->window) {
		    /* Same params: forget the new result and return the old */
		    fprintf(stderr,
			    "Discarding duplicate result for %g/%g/%c\n",
			    result->t, result->fft_freq,
			    window_key(result->window));
		    destroy_result(result);
		    return(r);
		}
	    }
	    /* rp points to the "next" field of the cell after which we
	     * should place it (or to the head pointer "results") */
	    result->next = *rp;
	    *rp = result;
	    if (r == NULL) last_result = result;
	}
    }
    return result;
}

/* Return the result for time t at the current fft_freq and window function
 * or NULL if it hasn't been calculated yet, in which case we schedule it.
 * speclen==-1 or window==-1 means "I don't care for what speclen/window".
 *
 * The parameter "fftfreq" may be the current fft_freq or ANY_FFTFREQ,
 * which will match the result for any FFT frequency.
 * Similarly for the parameter "window".
 */
calc_t *
recall_result(double t, double fftfreq, window_function_t window)
{
    calc_t *p;

    /* If it's later than the last cached result, we don't have it.
     * This saves uselessly scanning the whole list of results.
     */
    if (last_result == NULL || DELTA_GT(t, last_result->t))
	return(NULL);

    for (p=results; p != NULL; p=p->next) {
	/* If the time is the same and speclen is the same,
	 * this is the result we want */
	if (DELTA_GE(p->t, t) && DELTA_LE(p->t, t) &&
	    (fftfreq == ANY_FFTFREQ || p->fft_freq == fftfreq) &&
	    (window  == ANY_WINDOW  || p->window  == window)) {
	    break;
	}
	/* If the stored time is greater, it isn't there. */
	if (DELTA_GT(p->t, t)) {
	    p = NULL;
	    break;
	}
    }
    return(p);	/* NULL if not found */
}

/* Forget the result cache */
void
drop_all_results(void)
{
    calc_t *r;

    for (r = results; r != NULL; /* see below */) {
	calc_t *next = r->next;
	destroy_result(r);
	r = next;
    }
    results = last_result = NULL;
}

/* Free the memory associated with a result structure */
static void
destroy_result(calc_t *r)
{
    free(r->spec);
    free(r);
}
