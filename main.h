/*
 * main.h: Declarations for GUI state variables exported by main.c
 */

#ifndef MAIN_H

#include "config.h"
#include "calc.h"		/* for result_t */

#if ECORE_MAIN
#include <Ecore.h>
extern void calc_heavy(void *data, Ecore_Thread *thread);
extern void calc_notify(void *data, Ecore_Thread *thread, void *msg_data);
#elif SDL_MAIN
extern void *calc_heavy(void *data);
extern void calc_notify(result_t *result);
#else
# error "Define ECORE_MAIN or SDL_MAIN"
#endif

/* The callback function used to report results from FFT calculations */
extern void calc_result(result_t *result);

extern void repaint_display(void);
extern void do_scroll(void);

/* GUI state variables */
extern int disp_width;		/* Size of displayed drawing area in pixels */
extern int disp_height;
extern double disp_time; 	/* When in the audio file is the crosshair? */
extern int disp_offset;  	/* Crosshair is in which display column? */
extern double min_freq;		/* Range of frequencies to display: */
extern double max_freq;
extern double step;		/* time step per column = 1/ppsec */
extern bool piano_lines;	/* Draw lines where piano keys fall? */
extern bool staff_lines;	/* Draw manuscript score staff lines? */
extern bool guitar_lines;	/* Draw guitar string lines? */

/* Audio file info */
extern double audio_length;	/* Length of the audio in seconds */
extern double sample_rate;	/* SR of the audio in Hertz */

#define MAIN_H
#endif
