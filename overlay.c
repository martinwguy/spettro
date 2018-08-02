/*
 * overlay.c - Stuff to draw overlays of the piano keyboard and/or of
 * conventional score notation pentagrams.
 * RSN: Guitar strings!
 *
 * It's implemented by having an array with an element for each pixel
 * of a screen column, with each element saying whether there's an overlay
 * color at that height: 0 means no, 0xFFRRGGBB says of which colour if so.
 *
 * When the vertical axis is panned or zoomed, or the vertical window size
 * changes, the overlay matrix must be recalculated.
 */

#include "spettro.h"
#include "overlay.h"
#include "main.h"

#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include <string.h>	/* for memset() */

/* The array of overlay colours for every pixel column,
 * indexed from y=0 at the bottom to disp_height-1.
 */
static unsigned int *overlay = NULL;

/* We remember what we calculated it for so as to recalculate it
 * automatically if anything has changed.
 */
static double overlay_min_freq;
static double overlay_max_freq;
static int    overlay_len;

/*
 * Calculate the overlay
 *
 * Uses main's globals:
 *    double min_freq, double max_freq, 
 *    int disp_height,
 *    bool piano_lines, bool staff_lines
 */


void
make_overlay()
{
    int note;	/* Of 88-note piano: 0 = Bottom A, 87 = top C */
#define NOTE_A440	48  /* A above middle C */
    static double half_a_semitone = 0.0;
    int len = disp_height;
    
    if (half_a_semitone == 0.0)
	half_a_semitone = pow(2.0, 1/24.0);

    /* Check allocation of overlay array and zero it */
    if (overlay == NULL ) {
        overlay = malloc(len *sizeof(unsigned int));
      if (overlay == NULL )
          /* Continue with no overlay */
          return;
      }
      overlay_len = len;

    /* Check for resize */
    if (overlay_len != len) {
      overlay = realloc(overlay, len *sizeof(unsigned int));
      if (overlay == NULL )
          /* Continue with no overlay */
          return;
      overlay_len = len;
    }
    memset(overlay, 0, len * sizeof(unsigned int));

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
		overlay[magindex] = (color[note % 12] == 0)
				    ? 0xFFFFFFFF	/* 0=White */
				    : 0xFF000000;	/* 1=Black */
	}
    }

    if (staff_lines) {
	/* Which note numbers do the staff lines fall on? */
	static int notes[] = {
	    22, 26, 29, 32, 36,
	    43, 46, 50, 53, 56
	};
	int i;

	for (i=0; i < sizeof(notes)/sizeof(notes[0]); i++) {
	    double freq = note_to_freq(notes[i]);
	    int magindex = freq_to_magindex(freq);

	    /* Staff lines are 3 pixels wide */
	    if (magindex >= 0 && magindex < len)
		overlay[magindex] = 0xFFFFFFFF;
	    if (magindex-1 >= 0 && magindex-1 < len)
		overlay[magindex-1] = 0xFFFFFFFF;
	    if (magindex+1 >= 0 && magindex+1 < len)
		overlay[magindex+1] = 0xFFFFFFFF;
        }
    }
}

/* What colour overlays this pixel row?
 * 0x00000000 = Nothing
 * 0xFFrrggbb = this colour
 */
unsigned int
get_overlay(int y)
{
    if (overlay == NULL) return 0;

    /* If anything moved, recalculate the overlay.
     *
     * Changes to piano_lines or score_lines
     * will call make_overlay explicitly; these
     * others can change asynchronously.
     */
    if (overlay_min_freq != min_freq ||
	overlay_max_freq != max_freq ||
	overlay_len != disp_height - 1) {
	make_overlay();
	if (overlay == NULL) return 0;
	overlay_min_freq = min_freq;
	overlay_max_freq = max_freq;
	overlay_len = disp_height - 1;
    }

    return overlay[y];
}
