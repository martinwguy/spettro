/*
 * main.h: Declarations for GUI state variables exported by main.c
 */

#ifndef MAIN_H

#include "config.h"
#include "calc.h"		/* for result_t */

extern void repaint_display(bool repaint_all);
extern void do_scroll(void);
extern void paint_column(int pos_x, result_t *result);

/* GUI state variables */
extern int disp_width;		/* Size of displayed drawing area in pixels */
extern int disp_height;
extern double disp_time; 	/* When in the audio file is the crosshair? */
extern int disp_offset;  	/* Crosshair is in which display column? */
extern double min_freq;		/* Range of frequencies to display: */
extern double max_freq;
extern double min_db;		/* Values below this are black */
extern double ppsec;		/* pixel columns per second */
extern double step;		/* time step per column = 1/ppsec */
extern bool piano_lines;	/* Draw lines where piano keys fall? */
extern bool staff_lines;	/* Draw manuscript score staff lines? */
extern bool guitar_lines;	/* Draw guitar string lines? */
extern bool autoplay;		/* Start playing the file on startup? */

/* Audio file info */
extern double audio_length;	/* Length of the audio in seconds */
extern double sample_rate;	/* SR of the audio in Hertz */

#define MAIN_H
#endif
