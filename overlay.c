
/*
 * Stuff to draw overlays on the graphic
 *
 * - horizontal lines showing the frequencies of piano keys and/or
 *   of the conventional score notation pentagram lines or guitar strings.

 * - vertical lines to mark the bars and beats, user-adjustable
 *
 * == Row overlay ==
 *
 * The row overlay is implemented by having an array with an element for each
 * pixel of a screen column, with each element saying whether there's an overlay
 * color at that height: 0 means no, 0xFFRRGGBB says of which colour if so.
 *
 * When the vertical axis is panned or zoomed, or the vertical window size
 * changes, the row overlay matrix must be recalculated.
 *
 * == Column overlay ==
 *
 * The column overlay shows draggable bar lines a pixel wide (one day they
 * will be 3 pixels wide with intermediate beat markers 1 pixel wide).
 * The column overlay takes priority over the row overlay, so that
 * "bar lines" are maintained whole, not cut, and the bar lines overlay the
 * green line to avoid flashing them as they cross it.
 */

#include "spettro.h"
#include "overlay.h"
#include "gui.h"
#include "main.h"

#include <math.h>	/* for pow() */
#include <stdlib.h>	/* for malloc() */
#include <string.h>	/* for memset() */

static bool is_bar_line(int x);

/* The array of overlay colours for every pixel column,
 * indexed from y=0 at the bottom to disp_height-1
 */
static unsigned int *row_overlay = NULL;

/* and we remember what parameters we calculated it for so as to recalculate it
 * automatically if anything changes.
 */
static double row_overlay_min_freq;
static double row_overlay_max_freq;
static int    row_overlay_len;

/*
 * Calculate the overlays
 */

void
make_row_overlay()
{
    int note;	/* Of 88-note piano: 0 = Bottom A, 87 = top C */
#define NOTE_A440	48  /* A above middle C */
    int len = disp_height;

    /* Check allocation of overlay array and zero it */
    if (row_overlay == NULL ) {
        row_overlay = malloc(len *sizeof(unsigned int));
      if (row_overlay == NULL )
          /* Continue with no overlay */
          return;
      }
      row_overlay_len = len;

    /* Check for resize */
    if (row_overlay_len != len) {
      row_overlay = realloc(row_overlay, len *sizeof(unsigned int));
      if (row_overlay == NULL )
          /* Continue with no overlay */
          return;
      row_overlay_len = len;
    }
    memset(row_overlay, 0, len * sizeof(unsigned int));

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
	    if (magindex >= 0 && magindex < len)
		row_overlay[magindex] = (color[note % 12] == 0)
				    ? white : black;
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
	    if (magindex >= 0 && magindex < len)
		row_overlay[magindex] = white;
	    if (magindex-1 >= 0 && magindex-1 < len)
		row_overlay[magindex-1] = white;
	    if (magindex+1 >= 0 && magindex+1 < len)
		row_overlay[magindex+1] = white;
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
	    if (magindex >= 0 && magindex < len)
		row_overlay[magindex] = white;
	    if (magindex-1 >= 0 && magindex-1 < len)
		row_overlay[magindex-1] = white;
	    if (magindex+1 >= 0 && magindex+1 < len)
		row_overlay[magindex+1] = white;
        }
    }
}

/* What colour overlays this pixel row?
 * 0x00000000 = Nothing
 * 0xFFrrggbb = this colour
 */
unsigned int
get_row_overlay(int y)
{
    if (row_overlay == NULL) return 0;

    /* If anything moved, recalculate the overlay.
     *
     * Changes to piano_lines or score_lines
     * will call make_row_overlay explicitly; these
     * others can change asynchronously.
     */
    if (row_overlay_min_freq != min_freq ||
	row_overlay_max_freq != max_freq ||
	row_overlay_len != disp_height - 1)
    {
	make_row_overlay();
	if (row_overlay == NULL) return 0;
	row_overlay_min_freq = min_freq;
	row_overlay_max_freq = max_freq;
	row_overlay_len = disp_height - 1;
    }

    return row_overlay[y];
}

/*
 * Column overlays marking bar lines and beats
 *
 * The column overlays depend on the clicked start and end of a bar,
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

/* Set start and end of marked bar. */
void
set_bar_left_time(double when)
{
    if (when >= 0.0 - DELTA && when <= audio_length + DELTA) {
	bar_left_time = when;
	repaint_display(FALSE);
    }
}

void
set_bar_right_time(double when)
{
    if (when >= 0.0 && when <= audio_length + DELTA) {
	bar_right_time = when;
	repaint_display(FALSE);
    }
}

/*
 * What colour overlays this screen column?
 * 0 = none
 */
unsigned int
get_col_overlay(int x)
{
    return is_bar_line(x) ? white :
	   (x == disp_offset && !green_line_off) ? green :
	   0;
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

    /* They can set the "left" and "right" bar lines the other way round too */
    bar_width = bar_right_ticks - bar_left_ticks;
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
