/*
 * overlay.c - Stuff to draw row overlays on the graphic
 *
 * This draws horizontal lines across the whole graphic, showing the
 * conventional score notation pentagram lines, the frequencies of the
 * 88 piano keys or of the classical guitar's open strings.
 *
 * The row overlay is implemented by having an array with an element for each
 * pixel of a screen column, with each element saying whether there's an overlay
 * color at that height: 0 means no, 0xFFRRGGBB says of which colour if so.
 *
 * When the vertical axis is panned or zoomed, or the vertical window size
 * changes, the row overlay matrix must be recalculated.
 */

#include "spettro.h"
#include "overlay.h"

#include "convert.h"
#include "gui.h"
#include "ui.h"

#include <string.h>	/* for memset() */

static void overlay_row(int magindex, color_t color);

/* The array of overlay colours for every pixel column,
 * indexed the graphic's y-coordinate (not the whole screen's)
 */
static color_t	*row_overlay = NULL;
static bool	*row_is_overlaid = NULL;
static int       row_overlay_maglen = 0;

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
    int len = maglen;

    /* Check for resize */
    if (row_overlay_maglen != len) {
      row_overlay = Realloc(row_overlay, len * sizeof(*row_overlay));
      row_is_overlaid = Realloc(row_is_overlaid, len * sizeof(*row_is_overlaid));
      row_overlay_maglen = len;
    }
    memset(row_overlay, 0, len * sizeof(*row_overlay));
    memset(row_is_overlaid, 0, len * sizeof(*row_is_overlaid));

    if (piano_lines) {
	/* Run up the piano keyboard blatting the pixels they hit */
	for (note = 0; note < 88; note++) {
	    /* Colour of notes in octave,  starting from A */
	    static bool color[12] = { 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1 };
	    double freq = note_number_to_freq(note);
	    int magindex = freq_to_magindex(freq);

	    /* If in screen range, write it to the overlay */
	    if (magindex >= 0 && magindex < len) {
		overlay_row(magindex, color[note % 12] == 0 ? green : black);
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
	    double freq = note_number_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);
	    overlay_row(magindex, white);
	}
    }

    if (guitar_lines) {
	/* Which note numbers do the guitar strings fall on? */
	static int notes[] = {
	    19, 24, 29, 34, 38, 43  /* Classical guitar: E2 A2 D3 G3 B3 E4 */
	};
	int i;

	for (i=0; i < sizeof(notes)/sizeof(notes[0]); i++) {
	    double freq = note_number_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);
	    overlay_row(magindex, white);
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
	row_overlay_maglen != maglen)
    {
	make_row_overlay();
	if (row_overlay == NULL) return FALSE;
	row_overlay_min_freq = min_freq;
	row_overlay_max_freq = max_freq;
	row_overlay_maglen = maglen;
    }

    if (row_is_overlaid[magindex] && colorp != NULL)
	*colorp = row_overlay[magindex];
    return row_is_overlaid[magindex];
}
