/*
 * cache.c - Result cache
 *
 * We remember the FFT results we have calculated as result_t's, in time order,
 * which reflect the parameters that gave rise to that result and the
 * linear magnitude data.
 *
 * The mapping from this to screen coordinates and colors is done on-the-fly
 * at screen update time by paint_column() calling interpolate() and colormap().
 */

#include "spettro.h"
#include "calc.h"	/* for result_t */
#include "window.h"
#include "main.h"

#include <stdlib.h>	/* for free() */
#include <string.h>	/* for memcmp() */

static void destroy_result(result_t *r);

static result_t *results = NULL; /* Linked list of result structures */
static result_t *last_result = NULL; /* Last element in the linked list */

/* "result" was obtained from malloc(); it is up to us to free it. */
void
remember_result(result_t *result)
{
    /* Drop any stored results more than half a screenful before the display */
    while (results != NULL && results->t < disp_time - (disp_offset + disp_width/2) * step - DELTA) {
	result_t *r = results;
	results = results->next;
	destroy_result(r);
    }
    if (results == NULL) last_result = NULL;

    result->next = NULL;

    if (last_result == NULL) {
	results = last_result = result;
    } else {
        /* If after the last one, add at tail of list */
	if (result->t > last_result->t + DELTA) {
	    last_result->next = result;
	    last_result = result;
	} else {
	    /* If it's before the first one, tack it onto head of list */
	    if (result->t < results->t - DELTA) {
		result->next = results;
		results = result;
	    } else {
		/* Otherwise find which element to place it after */
		result_t *r;	/* The result after which we will place it */
		for (r=results;
		     r && r->next && r->next->t <= result->t + DELTA;
		     r = r->next) {
		    if (r->next->t <= result->t + DELTA &&
			r->next->t >= result->t - DELTA &&
			r->next->speclen == result->speclen &&
			r->next->window == result->window) {
			/* Same time, same size: forget it */
fprintf(stderr, "Destroying duplicate result for time %g with %s spectral data\n",
		result->t,
		memcmp(r->next->spec, result->spec, result->speclen * sizeof(result->spec[0]))
		    ? "different" : "the same");
			destroy_result(result);
			r = NULL; break;
		    }
		}
		if (r) {
		    result->next = r->next;
		    r->next = result;
		    if (last_result == r) last_result = result;
	        }
	    }
	}
    }
}

/* Return the result for time t at the current speclen and window function
 * or NULL if it hasn't been calculated yet, in which case we schedule it.
 * speclen==-1 or window==-1 means "I don't care for what speclen/window".
 */
result_t *
recall_result(double t, int speclen, window_function_t window)
{
    result_t *p;

    /* If it's later than the last cached result, we don't have it.
     * This saves uselessly scanning the whole list of results.
     */
    if (last_result == NULL || t > last_result->t + DELTA)
	return(NULL);

    for (p=results; p != NULL; p=p->next) {
	/* If the time is the same and speclen is the same,
	 * this is the result we want */
	if (p->t >= t - DELTA && p->t <= t + DELTA &&
	    (speclen == -1 || p->speclen == speclen) &&
	    (window == -1 || p->window == window)) {
	    break;
	}
	/* If the stored time is greater, it isn't there. */
	if (p->t > t + DELTA) {
	    p = NULL;
	    break;
	}
    }
    return(p);	/* NULL if not found */
}

/* Free the memory associated with a result structure */
static void
destroy_result(result_t *r)
{
    free(r->spec);
    free(r->mag);
    free(r);
}
