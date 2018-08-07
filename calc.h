/*
 * calc.h - header for interface to calc.c
 *
 * Calc performs FFTs for the whole file, calling the "notify" function
 * for each one it completes.
 * The result data is the linear FFT magnitudes from 0Hz to Nyquist frequency.
 */

#include <pthread.h>

#include "spettro.h"
#include "window.h"

/* The parameters to calc(), saying what it should FFT. */
typedef struct calc {
    audio_file_t *	audio_file; /* Our audio file handle */
    double		length;	/* Length of piece in seconds */
    double		sr;	/* Sample rate of the piece */
    double		from;	/* From how far into the piece... */
    double		to;	/* ...to when? 0.0 means to the end. */
    double		ppsec;	/* Pixel columns per second */
    int			speclen; /* Size of spectrum == fftsize/2 */
    enum WINDOW_FUNCTION window;
#if ECORE_MAIN
    Ecore_Thread *	thread; /* The thread this calculator is running in */
#elif SDL_MAIN
    pthread_t		thread;
#endif
} calc_t;

/*
 * Structure for results from calc() with the linear FFT for a pixel column,
 * callbacked to calc_result() as each column is ready.
 *
 * Fields mag and maglen are not filled by calc(); the main routine calls
 * interpolate() on each one to generate columns of magnitudes for the screen,
 * as the mapping changes when they pan up and down or zoom the Y axis,
 * without a need to recalculate the FFT. That is required if they change
 * the FFT size, while zooming the time axis just requires more results to
 * be calculated.
 */

typedef struct result {
    double t;		/* An FFT centred on what time through the piece? */
    int speclen;	/* The length of the linear spectrum */
    float *spec;	/* The linear spectrum from [0..speclen] = 0Hz..sr/2 */
    int maglen;		/* Length of magnitude data on log axis */
    float *mag;		/* Magnitude data from [0..maglen-1] */
#if ECORE_MAIN
    Ecore_Thread *thread; /* The calc thread that this result came from */
#elif SDL_MAIN
    pthread_t thread;
#endif
    struct result *next; /* Linked list of results in time order */
} result_t;

extern void calc(calc_t *data, void (*result_cb)(result_t *));
