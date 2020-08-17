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
 * do_key.c: Process key strokes.
 *
 * This file defines and implements the keyboard user interface.
 *
 * For brevity, this source file is written upside-down:
 * first, lots of little functions to perform all the possible operations,
 * then a table of key-values and which functions they call for the four
 * possible combinations of Shift and Ctrl,
 * then the public function, do_key(), which makes use of all the above.
 */

#include "spettro.h"
#include "do_key.h"

/*
 * Local header files
 */
#include "audio.h"
#include "audio_cache.h"
#include "axes.h"
#include "barlines.h"
#include "cache.h"
#include "colormap.h"
#include "convert.h"
#include "dump.h"
#include "gui.h"
#include "key.h"
#include "overlay.h"
#include "paint.h"
#include "scheduler.h"
#include "ui.h"
#include "ui_funcs.h"
#include "window.h"

static void
k_change_color(key_t key)
{
    change_colormap();
    repaint_display(TRUE);
}

static void
k_quit(key_t key)
{
    gui_quit_main_loop();
    quitting = TRUE;
}

static void
k_space(key_t key)	/* Play/Pause/Next_file/Quit */
{
    switch (playing) {
    case PLAYING:
	pause_audio();
	break;

    case STOPPED:
stopped:
	/* Move on to the next filename argument (or quit if it's the last) */
	gui_quit_main_loop();
	break;

    case PAUSED:
	if (DELTA_GE(get_playing_time(), audio_file_length())) {
	    /* They went "End" while it was paused. Do the same as STOPPED. */
	    goto stopped;
	}
	continue_playing();
	break;
    }
}

/* Play/Pause key - the same as Space */
static void
k_play(key_t key)
{
    k_space(key);
}

static void
k_stop(key_t key)
{
    if (playing == PLAYING) pause_audio();
}

/* Go to the next/previous audio file */
static void
k_next(key_t key)
{
    play_next = TRUE;
    gui_quit_main_loop();
}

static void
k_prev(key_t key)
{
    play_previous = TRUE;
    gui_quit_main_loop();
}

/*
 * Arrow <-/->: Jump back/forward a tenth of a screenful
 * With Shift, a whole screenful. With Ctrl one pixel.
 * With Ctrl-Shift, one second.
 */
static void
k_left_right(key_t key)
{
    double by;

    if (!Shift && !Ctrl) by = disp_width * secpp / 10;
    if (Shift && !Ctrl) by = disp_width * secpp;
    if (!Shift && Ctrl) by = secpp;
    if (Shift && Ctrl) by = 1.0;
    time_pan_by(key == KEY_LEFT ? -by : +by);
}

/*
 * Home and End: Go to start or end of piece
 */
static void
k_home(key_t key)
{
    set_playing_time(0.0);
    if (playing == STOPPED) playing = PAUSED;
}

static void
k_end(key_t key)
{
    /* Go to the last sample */
    set_playing_time(audio_file_length() - 1/current_sample_rate());
}

/*
 * Arrow Up/Down: Pan the frequency axis by a tenth of the screen height.
 * With Shift: by a screenful. With Ctrl, by a pixel, with both by a semitone.
 * The argument to freq_pan_by() multiplies min_freq and max_freq.
 * Page Up/Down: Pan the frequency axis by a screenful
 */
static void
k_freq_pan(key_t key)
{
    switch (key) {
    case KEY_UP:
	freq_pan_by((Ctrl && !Shift) ? v_pixel_freq_ratio():
		    (Shift && !Ctrl) ? max_freq / min_freq :
		    (Shift && Ctrl) ? pow(2, 1/12.0) :
		    pow(max_freq / min_freq, 1/10.0));
	break;
    case KEY_DOWN:
	freq_pan_by((Ctrl && !Shift) ? 1.0 / v_pixel_freq_ratio() :
		    (Shift && !Ctrl) ? min_freq / max_freq :
		    (Shift && Ctrl) ? 1.0 / pow(2, 1/12.0) :
		    pow(min_freq / max_freq, 1/10.0));
	break;
    case KEY_PGUP:
	freq_pan_by(max_freq/min_freq);
	break;
    case KEY_PGDN:
	freq_pan_by(min_freq/max_freq);
	break;
    default:
    	break;	/* Shut up compiler warnings */
    }

    gui_update_display();
}

/* Zoom on the time axis by a factor of two so that, when zooming in,
 * half of the results are still valid
 */
static void
k_time_zoom(key_t key)
{
    time_zoom_by(Shift ? 2.0 : 0.5);
    repaint_display(FALSE);
}

/* Y/y: Zoom in/out on the frequency axis.
 * With Shift, zoom in, without Shift, zoom out
 * Without Ctrl, by a factor of two; with Ctrl by one pixel top and bottom.
 */
static void
k_freq_zoom(key_t key)
{
    double by;	/* How much to zoom in by: >1 = zoom in, <1 = zoom out */

    if (Ctrl) by = (double)(max_y - min_y) / (double)((max_y-1) - (min_y+1));
    else by = 2.0;
    freq_zoom_by(Shift ? by : 1.0/by);
    repaint_display(TRUE);
}

/* Normal zoom-in zoom-out, i.e. both axes. */
static void
k_both_zoom_in(key_t key)
{
    freq_zoom_by(2.0);
    time_zoom_by(2.0);
    repaint_display(FALSE);
}

static void
k_both_zoom_out(key_t key)
{
    freq_zoom_by(0.5);
    time_zoom_by(0.5);
    repaint_display(FALSE);
}

/* Capital letters choose the window function */
static void
k_set_window(key_t key)
{
    window_function_t new_fn;

    switch (key) {
    case KEY_K: new_fn = KAISER;	break;
    case KEY_N: new_fn = NUTTALL;	break;
    case KEY_H: new_fn = HANN;		break;
    case KEY_B: new_fn = BLACKMAN;	break;
    case KEY_D: new_fn = DOLPH;		break;
    default:
	fprintf(stderr, "Internal error: Impossible window key %d\n", key);
    	return;
    }

    if (new_fn != window_function) {
	window_function = new_fn;
	if (show_time_axes) draw_status_line();
	drop_all_work();
	repaint_display(FALSE);
    }
}

/* C and c change the dynamic range of color spectrum by 6dB.
 * 'C' brightens the dark areas, which is achieved by increasing
 * the dynamic range;
 * 'c' darkens them to reduce visibility of background noise.
 * With Ctrl held, they brighten/darken by 1dB.
 */
static void
k_contrast(key_t key)
{
    float by = Ctrl ? 1.0 : 6.0;

    change_dyn_range(Shift ? by : -by);
    repaint_display(TRUE);
}
/* B and b increase/decrease the brightness of the graph by changing the
 * loudness that is represented by the brightest pixel color.
 */
static void
k_brightness(key_t key)
{
    float by = 0.6;	/* by 6 dB */

    /* Shift means "more brightness", which means diminishing logmax */
    change_logmax(Shift ? -by : by);
    repaint_display(TRUE);
}

/* Toggle frequency axis */
static void
k_toggle_axes(key_t key)
{
    if (!Shift) {	/* 'a': toggle frequency axes */
	if (show_freq_axes) {
	    /* Remove frequency axis */
	    min_x = 0; max_x = disp_width - 1;
	    show_freq_axes = FALSE;
	    repaint_columns(0, frequency_axis_width - 1, min_y, max_y, FALSE);
	    repaint_columns(max_x - note_name_axis_width, max_x, min_y, max_y, FALSE);
	} else {
	    /* Add frequency axis */
	    min_x = frequency_axis_width;
	    max_x = disp_width - 1 - note_name_axis_width;
	    show_freq_axes = TRUE;
	}
    } else {		/* 'A': Toggle time axis and status line */
	if (show_time_axes) {
	    /* Remove time axis and status information */
	    min_y = 0; max_y = disp_height - 1;
	    show_time_axes = FALSE;
	} else {
	    /* Add time axis */
	    min_y = bottom_margin;
	    max_y = disp_height - 1 - top_margin;
	    show_time_axes = TRUE;
	}
	/* Adding/removing the top and bottom axes scales the graph vertically
	 * so repaint. We only need to repaint unpainted columns if we are
	 * removing the axes, so that background color columns overwrite where
	 * the axes were. */
	repaint_columns(min_x, max_x, min_y, max_y, show_time_axes);
    }
    draw_axes();
}

/* w: Cycle through window functions;
 * W: cycle backwards
 */
static void
k_cycle_window(key_t key)
{
    window_function = ((!Shift) ? (window_function + 1)
    			        : (window_function + NUMBER_OF_WINDOW_FUNCTIONS-1))
    		      % NUMBER_OF_WINDOW_FUNCTIONS;
    if (show_time_axes) draw_status_line();
    drop_all_work();
    repaint_display(FALSE);
}

/* Toggle staff/piano/guitar line overlays */
static void
k_overlay(key_t key)
{
    switch (key) {
    case KEY_K:
	/* Cycle white keys through white-green-none */
	piano_lines = !piano_lines;
	break;
    case KEY_S:
	staff_lines = !staff_lines;
	if (staff_lines) guitar_lines = FALSE;
	break;
    case KEY_G:
	guitar_lines = !guitar_lines;
	if (guitar_lines) staff_lines = FALSE;
	break;
    default:	/* Silence compiler warnings */
    	break;
    }
    make_row_overlay();
    repaint_display(FALSE);
}

/* Make a screen dump */
static void
k_screendump(key_t key)
{
    dump_screenshot();
}

/* Print current UI parameters and derived values */
static void
k_print_params(key_t key)
{
    printf("filename=\"%s\"\n", current_audio_file()->filename);

    printf(
"min_freq=%g max_freq=%g dyn_range=%g logmax=%.3g fft_freq=%g window=%s\n",
 min_freq,   max_freq, (double)dyn_range, logmax, fft_freq,   window_name(window_function));

    printf("disp_time=%.3f ppsec=%.3f audio_length=%.3f jobs_in_flight=%d\n",
	disp_time, ppsec, audio_file_length(), jobs_in_flight);

    printf("%s %.3f Showing %.3f to %.3f\n",
	playing == PLAYING ? "Playing" :
	playing == STOPPED ? "Stopped at" :
	playing == PAUSED  ? "Paused at" : "Doing what? at",
	get_playing_time(),
	screen_column_to_start_time(min_x),
	screen_column_to_start_time(max_x + 1));

    if (left_bar_time != UNDEFINED)
	printf("left bar=%.3f", left_bar_time);
    if (right_bar_time != UNDEFINED) {
	if (left_bar_time != UNDEFINED) printf(" ");
	printf("right bar=%.3f", right_bar_time);
    }
    if (left_bar_time != UNDEFINED && right_bar_time != UNDEFINED)
	printf(" interval=%.3f beats_per_bar=%d bpm=%ld,",
	       fabs(right_bar_time - left_bar_time), beats_per_bar,
	       lrint(60.0 / fabs(right_bar_time - left_bar_time)
			       * beats_per_bar));
    if (left_bar_time != UNDEFINED || right_bar_time != UNDEFINED)
	printf("\n");
}

/* Display the current playing time */
static void
k_print_time(key_t key)
{
    printf("%02d:%02d (%g seconds)\n", (int) disp_time / 60,
				       (int) disp_time % 60,
				       disp_time);
}

/* Flip fullscreen mode */
static void
k_fullscreen(key_t key)
{
    gui_fullscreen();
}

/* Change FFT size: with Shift, double the sample size, without halve it */
static void
k_fft_size(key_t key)
{
    if (Shift) {
	/* Increase FFT size; decrease FFT frequency */
	if (DELTA_EQ(fft_freq, MIN_FFT_FREQ)) {
	    /* Already at the minimum value: do nothing */
	    return;       
	}
	fft_freq /= 2;
	if (DELTA_LT(fft_freq, MIN_FFT_FREQ)) {
	    fft_freq = MIN_FFT_FREQ;
	}
    } else {
	/* Decrease FFT size: increase FFT frequency */
	if (fft_freq_to_speclen(fft_freq, current_sample_rate()) > 1)
	    fft_freq *= 2;
    }
    reposition_audio_cache();
    drop_all_work();

    if (show_time_axes) draw_status_line();

    /* Any calcs that are currently being performed will deliver
     * a result for the old speclen, which will be ignored (or cached)
     */
    repaint_display(FALSE);
}

/* Set left or right bar line position to current play position */
static void
k_left_barline(key_t key)
{
    set_left_bar_time(disp_time);
}

static void
k_right_barline(key_t key)
{
    set_right_bar_time(disp_time);
}

static void
k_no_barlines(key_t key)
{
    left_bar_time = right_bar_time = UNDEFINED;
}

/* Refresh the display from the result cache */
static void
k_refresh(key_t key)
{
    repaint_display(FALSE);
}

/* Refresh the display recalculating all results from the audio */
static void
k_redraw(key_t key)
{
    drop_all_work();
    drop_all_results();
    repaint_display(FALSE);
}

/* softvol volume controls */
static void
k_softvol_down(key_t key)
{
    softvol *= 0.9;
    fprintf(stderr, "Softvol = %g\n", softvol);
}

static void
k_softvol_up(key_t key)
{
    softvol /= 0.9;
    fprintf(stderr, "Softvol = %g\n", softvol);
}

/* Beats per bar */
static void
k_beats_per_bar(key_t key)
{
    /* Under SDL2, Shift-number gives the punctuation characters above them
     * and Shift-Ctrl-number gives both Ctrl+Shift+N and the punctuation char.
     * Under SDL2, Ctrl-F1 to F12 seem not to be delivered. Shift-Fn yes.
     */
    switch (key) {
    case KEY_1: case KEY_F1:	set_beats_per_bar(1);	break;
    case KEY_2: case KEY_F2:	set_beats_per_bar(2);	break;
    case KEY_3: case KEY_F3:	set_beats_per_bar(3);	break;
    case KEY_4: case KEY_F4:	set_beats_per_bar(4);	break;
    case KEY_5: case KEY_F5:	set_beats_per_bar(5);	break;
    case KEY_6: case KEY_F6:	set_beats_per_bar(6);	break;
    case KEY_7: case KEY_F7:	set_beats_per_bar(7);	break;
    case KEY_8: case KEY_F8:	set_beats_per_bar(8);	break;
    case KEY_9:	case KEY_F9:	set_beats_per_bar(9);	break;
		case KEY_F10:	set_beats_per_bar(10);	break;
		case KEY_F11:	set_beats_per_bar(11);	break;
		case KEY_F12:	set_beats_per_bar(12);	break;
    default:
	fprintf(stderr, "Internal error: Impossible beats-per-bar key %d\n", key);
	return;
    }
}

static void
k_dump_audio_cache(key_t key)
{
    dump_audio_cache();
}

/* Now for the table of key-to-function mappings */

typedef struct {
    key_t key;
    char *name;
    void (*plain)(key_t key);
    void (*shift)(key_t key);
    void (*ctrl)(key_t key);
    void (*shiftctrl)(key_t key);
} key_fn;

static void
k_none(key_t key)
{
}

/* k_bad() needs key_fns[]; key_fns[] needs k_bad() */
static key_fn key_fns[];

static void
k_bad(key_t key)
{
    fprintf(stderr, "%s%s%s doesn't do anything\n",
    	Shift ? "Shift-" : "",
	Ctrl ? "Ctrl-" : "",
	key_fns[key].name);
}

/* This must have the same entries in the same order as "enum key" in key.h */
static key_fn key_fns[] = {
    /* KEY	Name    plain		Shift		Ctrl		ShiftCtrl */
    { KEY_NONE,	"None", k_none,		k_none,		k_none,		k_none },
    { KEY_Q,	"Q",	k_quit,		k_quit,		k_quit,		k_quit },
    { KEY_C,	"C",    k_contrast,	k_contrast,	k_contrast,	k_contrast },
    { KEY_ESC,	"Esc",  k_quit,		k_bad,		k_bad,		k_bad },
    { KEY_SPACE,"Space",k_space,	k_bad,		k_bad,		k_bad },
    { KEY_LEFT, "Left", k_left_right,	k_left_right,	k_left_right,	k_left_right },
    { KEY_RIGHT,"Right",k_left_right,	k_left_right,	k_left_right,	k_left_right },
    { KEY_HOME,	"Home",	k_home,		k_bad,		k_bad,		k_bad },
    { KEY_END,	"End",  k_end,		k_bad,		k_bad,		k_bad },
    { KEY_UP,	"Up",   k_freq_pan,	k_freq_pan,	k_freq_pan,	k_freq_pan },
    { KEY_DOWN,	"Down", k_freq_pan,	k_freq_pan,	k_freq_pan,	k_freq_pan },
    { KEY_PGUP,	"PageUp",  k_freq_pan,	k_bad,		k_bad,		k_bad },
    { KEY_PGDN,	"PageDown",k_freq_pan,	k_bad,		k_bad,		k_bad },
    { KEY_X,	"X",    k_time_zoom,	k_time_zoom,	k_bad,		k_bad },
    { KEY_Y,	"Y",    k_freq_zoom,	k_freq_zoom,	k_freq_zoom,	k_freq_zoom },
    { KEY_PLUS,	"Plus", k_softvol_up,	k_bad,		k_both_zoom_in,	k_bad },
    { KEY_MINUS,"Minus",k_softvol_down,	k_bad,		k_both_zoom_out,k_bad },
#if 0
    { KEY_STAR, "Star", k_bad		k_bad,		k_bad,		k_bad },
    { KEY_SLASH,"slash",k_bad		k_bad,		k_bad,		k_bad },
#endif
    { KEY_K,	"K",    k_overlay,	k_bad,		k_set_window,	k_bad },
    { KEY_S,	"S",    k_overlay,	k_bad,		k_bad,		k_bad },
    { KEY_G,	"G",    k_overlay,	k_bad,		k_bad,		k_bad },
    { KEY_O,	"O",    k_screendump,	k_bad,		k_bad,		k_bad },
    { KEY_P,	"P",    k_prev,		k_bad,		k_print_params,	k_bad },
    { KEY_T,	"T",    k_print_time,	k_bad,		k_bad,		k_bad },
    { KEY_F,	"F",    k_fft_size,	k_fft_size,	k_fullscreen,	k_bad },
    { KEY_L,	"L",    k_left_barline,	k_bad,		k_refresh,	k_bad },
    { KEY_R,	"R",    k_right_barline,k_bad,		k_redraw,	k_bad },
    { KEY_B,	"B",    k_brightness,	k_brightness,	k_set_window, 	k_bad },
    { KEY_D,	"D",    k_bad,		k_dump_audio_cache,k_set_window,k_bad },
    { KEY_A,	"A",    k_toggle_axes,	k_toggle_axes,	k_bad,		k_bad },
    { KEY_W,	"W",    k_cycle_window,	k_cycle_window,	k_bad,		k_bad },
    { KEY_M,	"M",    k_change_color,	k_bad,		k_bad,		k_bad },
    { KEY_H,	"H",	k_bad,		k_bad,		k_set_window,	k_bad },
    { KEY_N,	"N",    k_next,		k_bad,		k_set_window,	k_bad },
    { KEY_0,	"0",	k_no_barlines,	k_bad,		k_bad,		k_bad },
    { KEY_9,	"9",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_1,	"1",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_2,	"2",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_3,	"3",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_4,	"4",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_5,	"5",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_6,	"6",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_7,	"7",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_8,	"8",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F1,	"F1",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F2,	"F2",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F3,	"F3",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F4,	"F4",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F5,	"F5",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F6,	"F6",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F7,	"F7",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F8,	"F8",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F9,	"F9",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F10,	"F10",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F11,	"F11",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    { KEY_F12,	"F12",	k_beats_per_bar,k_bad,		k_bad,		k_bad },
    /* Extended keyboard's >/|| [] |<< and >>| buttons */
    { KEY_PLAY,	"Play",	k_play,		k_bad,		k_bad,		k_bad },
    { KEY_STOP,	"Stop",	k_stop,		k_bad,		k_bad,		k_bad },
    { KEY_PREV,	"Prev",	k_prev,		k_bad,		k_bad,		k_bad },
    { KEY_NEXT,	"Next",	k_next,		k_bad,		k_bad,		k_bad },
};
#define N_KEYS (sizeof(key_fns) / sizeof(key_fns[0]))

/* External interface */

void
do_key(key_t key)
{
    int i;

    for (i=0; i < N_KEYS; i++) {
    	if (key_fns[i].key != i) {
	    fprintf(stderr, "Key function table is skewed at element %d\n", i);
	    return;
	}
	if (key_fns[i].key == key) {
	    if (!Shift && !Ctrl){ (*key_fns[i].plain)(key);	return; }
	    if (Shift && !Ctrl)	{ (*key_fns[i].shift)(key);	return; }
	    if (!Shift && Ctrl)	{ (*key_fns[i].ctrl)(key);	return; }
	    if (Shift && Ctrl)	{ (*key_fns[i].shiftctrl)(key);	return; }
	}
    }
    fprintf(stderr, "Internal error: Impossible key value %d\n", key);
}
