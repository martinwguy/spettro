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
 * dump.c: Screen-dumping and (one day) entire spectrogram-writing routines
 */

#include "spettro.h"
#include "dump.h"

#include "audio_file.h"		/* for audio_file */
#include "barlines.h"
#include "gui.h"
#include "ui.h"

#include <libgen.h>		/* for basename() */
#include <string.h>

void
dump_screenshot()
{
    char s[1024];
    char *filename = "spettro";

    strcpy(s, filename);
    /* Add any parameters that they've changed */
#define add(s, fmt, val) sprintf(s + strlen(s), fmt, val)
    if (disp_width != DEFAULT_DISP_WIDTH)     add(s, " -w %d", disp_width);
    if (disp_height != DEFAULT_DISP_HEIGHT)   add(s, " -h %d", disp_height);
    if (DELTA_NE(min_freq, DEFAULT_MIN_FREQ)) add(s, " -n %g", min_freq);
    if (DELTA_NE(max_freq, DEFAULT_MAX_FREQ)) add(s, " -x %g", max_freq);
    if (DELTA_NE(dyn_range,DEFAULT_DYN_RANGE))add(s, " -d %g", dyn_range);
    if (DELTA_NE(fps, DEFAULT_FPS))           add(s, " -S %g", fps);
    if (DELTA_NE(ppsec, DEFAULT_PPSEC))       add(s, " -P %g", ppsec);
    if (DELTA_NE(fft_freq, DEFAULT_FFT_FREQ)) add(s, " -f %g", fft_freq);
    if (DELTA_NE(window_function, DEFAULT_WINDOW_FUNCTION))
    					      add(s, " -W%c", window_key(window_function));
    if (DELTA_NE(disp_time, 0.0))             add(s, " -t %g", disp_time);
    if (DELTA_NE(logmax, DEFAULT_LOGMAX))     add(s, " -M %.3g", logmax);
    if (piano_lines)      add(s, " %s", "-k");
    if (staff_lines)      add(s, " %s", "-s");
    if (guitar_lines)     add(s, " %s", "-g");
    if (show_freq_axes)   add(s, " %s", "-a");
    if (show_time_axes)   add(s, " %s", "-A");
    if (left_bar_time != UNDEFINED)  add(s, " -l %g", left_bar_time);
    if (right_bar_time != UNDEFINED) add(s, " -r %g", right_bar_time);
    if (beats_per_bar != DEFAULT_BEATS_PER_BAR) add(s, " -b %d", beats_per_bar);

    {
	/* basename() man modify the string, so work on a copy */
	char *fn = strdup(current_audio_file()->filename);
	char *bn = basename(fn);
	char *dot = strrchr(bn, '.');

	if (dot) *dot = '\0';
	add(s, " %s.png", bn);
	free(fn);
    }

#undef add
    gui_output_png_file(s);
}
