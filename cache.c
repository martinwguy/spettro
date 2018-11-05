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

#include <string.h>	/* for memcmp() */
#include <assert.h>

static void destroy_result(result_t *r);

static result_t *results = NULL; /* Linked list of result structures */
static result_t *last_result = NULL; /* Last element in the linked list */

/* "result" was obtained from malloc(); it is up to us to free it. */
/* We return the result because, if we find a duplicate in the cache, that
 * becomes the active result.
 */
result_t *
remember_result(result_t *result)
{
    /* Drop any stored results more than a screenful before the display */
    while (results != NULL && results->t < disp_time - (disp_offset + disp_width) * step - DELTA) {
	result_t *r = results;
	results = results->next;
	destroy_result(r);
    }

    /* Now find where to add the result to the time-ordered list */
    if (results == NULL) {
	assert(last_result == NULL);
        result->next = NULL;
	results = last_result = result;
    } else {
        /* If it's after the last one (the most common case),
	 * add it at the tail of the list
	 */
	if (result->t > last_result->t + DELTA) {
            result->next = NULL;
	    last_result->next = result;
	    last_result = result;
	} else {
	    /* If it's before the first one, tack it onto head of list,
	     * otherwise find which element to place it after
	     */
	    result_t **rp;	/* Pointer to "next" field of previous result
			     * or to "results": the pointer we'll have
			     * to update when we've found the right place.
			     */
	    result_t *r;	/* Handy pointer to the result to examine */

	    for (rp=&results;
		 (r = *rp) != NULL && r->t <= result->t + DELTA;
		 rp = &((*rp)->next)) {
		/* Check for duplicates */
		if (r->t <= result->t + DELTA &&
		    r->t >= result->t - DELTA &&
		    r->speclen == result->speclen &&
		    r->window == result->window) {
		    /* Same params: forget the new result and return the old */
fprintf(stderr, "Discarding duplicate result for time %g speclen %d window %d\n", result->t, result->speclen, result->window);
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

/* Forget the result cache */
void
drop_all_results(void)
{
    result_t *r;

    for (r = results; r != NULL; /* see below */) {
	result_t *next = r->next;
	destroy_result(r);
	r = next;
    }
    results = last_result = NULL;
}

/* Free the memory associated with a result structure */
static void
destroy_result(result_t *r)
{
    free(r->spec);
    free(r);
}
