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
 * ui.h - Header for the ui.c and its callers
 *        and default values of its settings.
 *
 * Herein should be all variables that determine
 * what the screen view looks like.
 */

#include "spettro.h"
#include "ui.h"

#include "barlines.h"	/* for UNDEFINED */

/* UI state variables */

/* Size of display area in pixels */
unsigned disp_width	= DEFAULT_DISP_WIDTH;
unsigned disp_height	= DEFAULT_DISP_HEIGHT;

/* Range of frequencies to display */
double min_freq	= DEFAULT_MIN_FREQ;
double max_freq	= DEFAULT_MAX_FREQ;	

/* Dynamic range of color map (values below this are black) */
double min_db	= DEFAULT_MIN_DB;

/* How many video output frames to generate per second while playing
 * and how many pixel columns to generate per second of the audio file.
 * If they are equal, the graphic should scroll by one pixel column at a time.
 */
double fps	= DEFAULT_FPS;
double ppsec	= DEFAULT_PPSEC;

/* The "FFT frequency": 1/fft_freq seconds of audio are windowed and FFT-ed */
double fft_freq	= DEFAULT_FFT_FREQ;

/* Which window functions to apply to each audio sample before FFt-ing it */
window_function_t window_function = DEFAULT_WINDOW_FUNCTION;

bool piano_lines  = FALSE;	/* Draw lines where piano keys fall? */
bool staff_lines  = FALSE;	/* Draw manuscript score staff lines? */
bool guitar_lines = FALSE;	/* Draw guitar string lines? */
bool show_axes = FALSE;		/* Are we to show/showing the axes? */

double left_bar_time = UNDEFINED;
double right_bar_time = UNDEFINED;
int beats_per_bar = DEFAULT_BEATS_PER_BAR;

/* Other option flags */
bool autoplay = FALSE;		/* -p  Start playing on startup */
bool exit_when_played = FALSE;	/* -e  Exit when the file has played */
bool fullscreen = FALSE;	/* Start up in fullscreen mode? */
int min_x, max_x, min_y, max_y;	/* Edges of graph in display coords */
bool green_line_off = FALSE;	/* Should we omit it when repainting? */
double softvol = 1.0;
int max_threads = 0;		/* 0 means use default (the number of CPUs) */
char *output_file = NULL;	/* Image file to write to and quit. This is done
       				 * when the last result has come in from the
				 * FFT threads, in calc_notify in scheduler.c */

/* Where is time and space is the current playing position on the scren? */
double disp_time = 0.0;	/* When in the audio file is the crosshair? */
int disp_offset; 	/* Crosshair is in which display column? */

/* The size of the vertical axes, when they are present. */

/* Numeric frequency axis on the left.
 * 22050- == Space, Five * (digit + blank column) + 2 pixels for tick.
 * Will be increased if 100000 or 0.00001 are displayed
 */
unsigned frequency_axis_width = 1 + 5 * (3 + 1) + 2;	/* == 23 */

/* Frequency axis of note names on the right.
 * -A0 == Two pixels for tick, a space, two * (letter + blank column)
 */
unsigned note_name_axis_width = 2 + 1 + 2 * (3 + 1);	/* == 11 */

/* Space above and below for other axes/info. None yet. */
unsigned top_margin = 0;
unsigned bottom_margin = 0;

/* Values derived from the above */
double step = 0.0;	/* time step per column = 1/ppsec
			 * 0.0 means "not set yet" as a booby trap. */
int speclen;		/* Size of linear spectral data */
int maglen;		/* Size of logarithmic spectral data
			 * == height of graph in pixels */
