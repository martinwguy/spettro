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

/* ui.h - Header for the ui.c and its callers
 * and default values of its settings.
 */

#include "gui.h"	/* for color_t */
#include "window.h"

/* UI state variables */

/* Size of display area in pixels */
extern unsigned disp_width;
extern unsigned disp_height;
#define DEFAULT_DISP_WIDTH	640
#define DEFAULT_DISP_HEIGHT	480

/* Range of frequencies to display */
extern double min_freq, max_freq;
/* Default values: 9 octaves from A0 to A9 */
#define DEFAULT_MIN_FREQ	27.5
#define DEFAULT_MAX_FREQ	14080.0

/* Dynamic range of color map (values below minus this are black) */
extern double dyn_range;
#define DEFAULT_DYN_RANGE	(96.0)
extern double logmax;
#define DEFAULT_LOGMAX		(0.0)	/* 0 = log10(1.0) */

/* Screen-scroll frequency and number of pixel columns per second of audio */
extern double fps;
extern double ppsec;
#define DEFAULT_FPS	50.0
#define DEFAULT_PPSEC	50.0

/* The "FFT frequency": 1/fft_freq seconds of audio are windowed and FFT-ed */
extern double fft_freq;
#define DEFAULT_FFT_FREQ 5.0

/* Which window functions to apply to each audio sample before FFt-ing it */
extern window_function_t window_function;
#define DEFAULT_WINDOW_FUNCTION KAISER

/* The -t/--start time parameter */
extern double start_time;

extern bool piano_lines;	/* Draw lines where piano keys fall? */
extern bool staff_lines;	/* Draw manuscript score staff lines? */
extern bool guitar_lines;	/* Draw guitar string lines? */
extern color_t piano_line_color; /* Color to draw piano lines' white keys */
#define DEFAULT_PIANO_LINE_COLOR white
extern color_t staff_line_color; /* Color to draw staff lines */
#define DEFAULT_STAFF_LINE_COLOR white
extern color_t guitar_line_color;/* Color to draw guitar lines */
#define DEFAULT_GUITAR_LINE_COLOR white
extern int staff_line_width;	/* Thickness of staff lines (1 or 3) */
extern int guitar_line_width;	/* Thickness of guitar lines (1 or 3) */
extern bool show_freq_axes;	/* Are we to show/showing the v. axis? */
extern bool show_time_axes;	/* Are we to show/showing the h. axis? */

/* Markers for the start and end of one bar, measured in seconds from
 * the start of the piece. */
extern double left_bar_time, right_bar_time;
/* Number of beats per bar. If >1, the bar lines become 3 pixels wide. */
extern int beats_per_bar;
#define DEFAULT_BEATS_PER_BAR 1

/* Other option flags */
extern bool autoplay;		/* -p  Start playing on startup */
extern bool exit_when_played;	/* -e  Exit when the file has played */
extern bool fullscreen;		/* Start up in fullscreen mode? */
extern int min_x, max_x, min_y, max_y;	/* Edges of graph in display coords */
extern bool green_line_off;	/* Should we omit it when repainting? */
extern double softvol;
extern int max_threads;		/* 0 means use default (the number of CPUs) */
extern char *output_file;	/* Image file to write to */

/* End of option flags. Derived and calculated parameters follow */

/* Where is time and space is the current playing position on the screen? */
extern double disp_time;	/* When in the audio file is the crosshair? */
extern int disp_offset; 	/* Crosshair is in which display column? */

extern unsigned frequency_axis_width;	/* Left axis area */
extern unsigned note_name_axis_width;	/* Right axis area */
extern unsigned top_margin, bottom_margin; /* Top and bottom axes heights */

/* Values derived from the above */
#define secpp	(1 / ppsec)		/* time step per column */
#define maglen	(max_y - min_y + 1)	/* Size of logarithmic spectral data
			 		 * == height of graph in pixels */

/* Variables to control the main loop from the depths of do_key.c */
extern bool play_previous;	/* Have they asked to play the previous file? */
extern bool play_next;		/* Have they asked to play the next file? */
extern bool quitting;		/* Have they asked to quit the program? */
