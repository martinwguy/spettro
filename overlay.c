/*
 * overlay.c - Stuff to draw overlays on the graphic
 *
 * - horizontal lines showing the frequencies of piano keys and/or
 *   of the conventional score notation pentagram lines or guitar strings.
 * - vertical lines to mark the bars and beats, user-adjustable
 *
 * == Row overlays ==
 *
 * The row overlay is implemented by having an array with an element for each
 * pixel of a screen column, with each element saying whether there's an overlay
 * color at that height: 0 means no, 0xFFRRGGBB says of which colour if so.
 *
 * When the vertical axis is panned or zoomed, or the vertical window size
 * changes, the row overlay matrix must be recalculated.
 *
 * == Column overlays ==
 *
 * The column overlay shows bar lines a pixel wide.  One day they will
 * be 3 pixels wide with intermediate beat markers 1 pixel wide.
 * The column overlay takes priority over the row overlay, so that
 * "bar lines" are maintained whole, not cut, and the bar lines overlay the
 * green line to avoid flashing them as they cross it.
 */

#include "spettro.h"
#include "overlay.h"
#include "gui.h"
#include "main.h"

#include <math.h>	/* for pow() */
#include <string.h>	/* for memset() */

static bool is_bar_line(int x);
static void overlay_row(int magindex, color_t color);

/* The array of overlay colours for every pixel column,
 * indexed the graphic's y-coordinate (not the whole screen's)
 */
static color_t	*row_overlay = NULL;
static bool	*row_is_overlaid = NULL;
static int       row_overlay_len = 0;

/* and we remember what parameters we calculated it for so as to recalculate it
 * automatically if anything changes.
 */
static double row_overlay_min_freq;
static double row_overlay_max_freq;

/*
 * Calculate the overlays
 */

void
make_row_overlay()
{
    int note;	/* Of 88-note piano: 0 = Bottom A, 87 = top C */
#define NOTE_A440	48  /* A above middle C */
    int len = maglen;

    /* Check for resize */
    if (row_overlay_len != len) {
      row_overlay = Realloc(row_overlay, len * sizeof(*row_overlay));
      row_is_overlaid = Realloc(row_is_overlaid, len * sizeof(*row_is_overlaid));
      row_overlay_len = len;
    }
    memset(row_overlay, 0, len * sizeof(*row_overlay));
    memset(row_is_overlaid, 0, len * sizeof(*row_is_overlaid));

    if (piano_lines) {
	/* Run up the piano keyboard blatting the pixels they hit */
	for (note = 0; note < 88; note++) {
	    /* Colour of notes in octave,  starting from A */
	    static bool color[12] = { 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1 };

#define note_to_freq(note) (440.0 * pow(2.0, (1/12.0) * (note - NOTE_A440)))
#define freq_to_magindex(freq)	lrint((log(freq) - log(min_freq)) /	\
				(log(max_freq) - log(min_freq)) *	\
				(disp_height - 1))

	    double freq = note_to_freq(note);
	    int magindex = freq_to_magindex(freq);

	    /* If in screen range, write it to the overlay */
	    if (magindex >= 0 && magindex < len) {
		overlay_row(magindex, color[note % 12] == 0 ? white : black);
	    }
	}
    }

    if (staff_lines) {
	/* Which note numbers do the staff lines fall on? */
	static int notes[] = {
	    22, 26, 29, 32, 36,	/* G2 B2 D3 F3 A3 */
	    43, 46, 50, 53, 56	/* E4 G4 B4 D5 F5 */
	};
	int i;

	for (i=0; i < sizeof(notes)/sizeof(notes[0]); i++) {
	    double freq = note_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);

	    /* Staff lines are 3 pixels wide */
	    overlay_row(magindex-1, white);
	    overlay_row(magindex,   white);
	    overlay_row(magindex+1, white);
	}
    }

    if (guitar_lines) {
	/* Which note numbers do the guitar strings fall on? */
	static int notes[] = {
	    19, 24, 29, 34, 38, 43  /* Classical guitar: E2 A2 D3 G3 B3 E4 */
	};
	int i;

	for (i=0; i < sizeof(notes)/sizeof(notes[0]); i++) {
	    double freq = note_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);

	    /* Guitar lines are also 3 pixels wide */
	    overlay_row(magindex-1, white);
	    overlay_row(magindex,   white);
	    overlay_row(magindex+1, white);
        }
    }
}

/*
 * Remember that there's an overlay on this row of the graphic.
 * Out-of-range values of "magindex" are silently ignored.
 */
static void
overlay_row(int magindex, color_t color)
{
    if (magindex >= 0 && magindex < maglen) {
	row_overlay[magindex] = color;
	row_is_overlaid[magindex] = TRUE;
    }
}

void
free_row_overlay()
{
    free(row_overlay);
    row_overlay = NULL;
}

/* What colour overlays this pixel row?
 * 
 * Returns TRUE if this row is overlaid and fills "ov" if non-NULL
 * Returns FALSE if there is no overlay on this row.
 */
bool
get_row_overlay(int y, color_t *colorp)
{
    int magindex = y - min_y;

    if (row_overlay == NULL) return FALSE;

    /* If anything moved, recalculate the overlay.
     *
     * Changes to piano_lines or score_lines
     * will call make_row_overlay explicitly; these
     * others can change asynchronously.
     */
    if (row_overlay_min_freq != min_freq ||
	row_overlay_max_freq != max_freq ||
	row_overlay_len != maglen)
    {
	make_row_overlay();
	if (row_overlay == NULL) return FALSE;
	row_overlay_min_freq = min_freq;
	row_overlay_max_freq = max_freq;
	row_overlay_len = maglen - 1;
    }

    if (row_is_overlaid[magindex] && colorp != NULL)
	*colorp = row_overlay[magindex];
    return row_is_overlaid[magindex];
}

/* ======================  Column overlays ====================== */

/*
 * Column overlays are used to marking bar lines and beats.
 *
 * The column overlays depend on the start and end of a bar,
 * measured in pixels, not time, for convenience.
 * If a beat line doesn't fall exactly on a pixel's timestamp, we round
 * it to the nearest pixel.
 */

/* Markers for start and end of bar in pixels from the start of the piece.
 *
 * Maybe: with no beats, 1-pixel-wide bar line.
 * With beats, 3 pixels wide.
 */
#define UNDEFINED (-1.0)
static double bar_left_time = UNDEFINED;
static double bar_right_time = UNDEFINED;
/* The bar position converted to a pixel index into the whole piece */
#define bar_left_ticks (lrint(bar_left_time / step))
#define bar_right_ticks (lrint(bar_right_time / step))

/* Set start and end of marked bar.
 * If neither is defined, we display nothing.
 * If only one is defined or they are the same, display a marker at that point.
 * If both are defined, we display both and other barlines at the same interval.
 *
 * We used to repaint the whole display every time, slow slow. Now when
 * bar line positions change, we wipe out the already-displayed ones and
 * redraw the new ones, which is much faster.
 */
void
set_bar_left_time(double when)
{
    if (bar_right_time == UNDEFINED) {
        int new_col;
	/* Move the sole left marker */
	if (bar_left_time != UNDEFINED) {
	    int old_col = disp_offset + lrint((bar_left_time - disp_time) / step);
	    bar_left_time = when;
	    if (old_col >= min_x && old_col <= max_x) {
		repaint_column(old_col, min_y, max_y, FALSE);
		gui_update_column(old_col);
	    }
	}
	bar_left_time = when;
	new_col = disp_offset + floor((when - disp_time) / step);
	repaint_column(new_col, min_y, max_y, FALSE);
	gui_update_column(new_col);
    } else {
	if (bar_left_time != UNDEFINED) {
	    double old_bar_left_time = bar_left_time;
	    int col;
	    /* Both left and right were already defined so clear existing bar lines */
	    for (col=min_x; col <= max_x; col++) {
		if (is_bar_line(col)) {	
		    bar_left_time = when;
		    repaint_column(col, min_y, max_y, FALSE);
		    gui_update_column(col);
		    bar_left_time = old_bar_left_time;
		}
	    }
	}
	/* and paint the new bar lines */
    	bar_left_time = when;
	{   int col;
	    for (col=min_x; col <= max_x; col++) {
		if (is_bar_line(col)) {
		    repaint_column(col, min_y, max_y, FALSE);
		    gui_update_column(col);
		}
	    }
	}
    }
}

void
set_bar_right_time(double when)
{
    if (bar_left_time == UNDEFINED) {
        int new_col;
	/* Move the sole right marker */
	if (bar_right_time != UNDEFINED) {
	    int old_col = disp_offset + lrint((bar_right_time - disp_time)/step);
	    bar_right_time = when;
	    if (old_col >= min_x && old_col <= max_x) {
		repaint_column(old_col, min_y, max_y, FALSE);
		gui_update_column(old_col);
	    }
	}
	bar_right_time = when;
	new_col = disp_offset + floor((when - disp_time) / step);
	repaint_column(new_col, min_y, max_y, FALSE);
	gui_update_column(new_col);
    } else {
	if (bar_right_time != UNDEFINED) {
	    double old_bar_right_time = bar_right_time;
	    int col;
	    /* Both left and right were already defined so clear existing bar lines */
	    for (col=min_x; col <= max_x; col++) {
		if (is_bar_line(col)) {	
		    bar_right_time = when;
		    repaint_column(col, min_y, max_y, FALSE);
		    gui_update_column(col);
		    bar_right_time = old_bar_right_time;
		}
	    }
	}
	/* and paint the new bar lines */
    	bar_right_time = when;
	{   int col;
	    for (col=min_x; col <= max_x; col++) {
		if (is_bar_line(col)) {
		    repaint_column(col, min_y, max_y, FALSE);
		    gui_update_column(col);
		}
	    }
	}
    }
}

/*
 * What colour overlays this screen column?
 * 
 * Returns TRUE if there is a column overlay, FALSE if not and writes
 * the color into what colorp points at if it's not NULL.
 */
bool
get_col_overlay(int x, color_t *colorp)
{
    bool is_overlayed = FALSE;
    color_t color;

    /* Bar lines take priority over the green line so that they don't
     * appear to flash as they cross it while playing and so that you can
     * see when you have placed a bar line at the current playing position
     * when it's paused. */
    if (is_bar_line(x)) {
	color = white; is_overlayed = TRUE; 
    } else
    if (x == disp_offset && !green_line_off) {
	color = green; is_overlayed = TRUE;
    }

    if (is_overlayed && colorp != NULL) *colorp = color;

    return is_overlayed;
}

/* Does screen column x coincide with the position of a bar line? */
static bool
is_bar_line(int x)
{
    int bar_width;	/* How long is the bar in pixels? */

    /* Convert screen-x to column index into the whole piece */
    x += lrint(disp_time / step) - disp_offset;

    /* If neither of the bar positions is defined, there are none displayed */
    if (bar_left_time == UNDEFINED &&
	bar_right_time == UNDEFINED) return FALSE;

    bar_width = bar_right_ticks - bar_left_ticks;
    /* They can set the "left" and "right" bar lines the other way round too */
    if (bar_width < 0) bar_width = -bar_width;

    /* If only one of the bar positions is defined, only that one is displayed.
     * Idem if they've defined both bar lines in the same pixel column.
     *
     * Both UNDEFINED is handled above; if either are UNDEFINED here,
     * bar_*_ticks will not be called.
     */
    if (bar_left_time == UNDEFINED ||
	bar_right_time == UNDEFINED ||
	bar_left_ticks == bar_right_ticks) {

	return x == bar_left_ticks || x == bar_right_ticks;
    }

    /* Both bar positions are defined. See if this column falls on one. */
    return x % bar_width == bar_left_ticks % bar_width;
}
