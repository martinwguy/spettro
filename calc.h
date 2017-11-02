/*
 * calc.h - header for interface to calc.c
 *
 * Calc performs FFTs for the whole file, calling the "notify" function
 * for each one it completes.
 * The result data is the linear FFT magnitudes from 0Hz to Nyquist frequency.
 */

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
    Ecore_Thread *thread; /* The thread this calculator is running in */
    void		*data;	/* Extra stuff not needed by the calc thread */
} calc_t;

typedef struct result {
    double t;		/* An FFT centred on what time through the piece? */
    int speclen;	/* The length of the linear spectrum */
    float *spec;	/* The linear spectrum from [0..speclen] = 0Hz..sr/2 */
    int maglen;		/* Length of magnitude data on log axis */
    float *mag;		/* Magnitude data from [0..maglen-1] */
    Ecore_Thread *thread; /* The calc thread that this result came from */
    /* Linked list of results, not in any particular order */
    struct result *next;
} result_t;

extern void calc(calc_t *data, void (*result_cb)(result_t *));
