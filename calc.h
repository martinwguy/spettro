/*
 * calc.h - header for interface to calc.c
 *
 * Calc performs FFTs for the whole file, calling the "notify" function
 * for each one it completes.
 * The result data is the linear FFT magnitudes from 0Hz to Nyquist frequency.
 */

#include <audiofile.h>
#include <Ecore.h>

#include "window.h"

/* The parameters to calc(), saying what it should FFT. */
typedef struct calc {
    AFfilehandle	af;	/* The audio file opened with libaudiofile */
    double		length;	/* Length of piece in seconds */
    double		sr;	/* Sample rate of the piece */
    double		from;	/* From how far into the piece... */
    double		to;	/* ...to when? 0.0 means to the end. */
    double		ppsec;	/* Pixel columns per second (or n_results?) */ 
    int			speclen; /* Size of spectrum == fftsize/2 */
    enum WINDOW_FUNCTION window;
} calc_t;

typedef struct result {
    double t;		/* An FFT centred on what time through the piece? */
    int speclen;	/* The length of the magnitude data */
    float *spec;	/* The spectrum from [0..speclen] */
    /* Linked list of results, not in any particular order */
    struct result *next;
} result_t;

extern void calc_heavy(void *data, Ecore_Thread *thread);
