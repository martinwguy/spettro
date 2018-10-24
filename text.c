/* text.c: Stuff for drawing text on the display */

#include "spettro.h"
#include "text.h"

#include "gui.h"
#include "lock.h"

#include <ctype.h>
#include <string.h>

/* We just have our own few-pixels-high font */

static char *digits[] = {
    "000", " 0 ", "00 ", "000", "0 0", "000", "000", "000", "000", "000",
    "0 0", "00 ", "  0", "  0", "0 0", "0  ", "0  ", "  0", "0 0", "0 0",
    "0 0", " 0 ", " 0 ", "000", "000", "00 ", "000", " 0 ", "000", "000",
    "0 0", " 0 ", "0  ", "  0", "  0", "  0", "0 0", "0  ", "0 0", "  0",
    "000", "000", "000", "000", "  0", "00 ", "000", "0  ", "000", "000",
};

/*
 * Draw the given text at the given coordinates.
 * Alignment TOP put the top pixel of the text at that y coordinat
 * Alignment LEFT puts the leftmost pixel of the text at that x coordinat
 * CENTER centers the text on that coordinate value.
 */
void
draw_text(char *text, int at_x, int at_y,
		      alignment_t alignment_x, alignment_t alignment_y)
{
    int width = strlen(text) * 4 - 1;
    int height = 5;
    int x;	/* index into the string */

    /* Make at_x and at_y the position of the top left pixel of the text */
    switch (alignment_x) {
    case LEFT:	at_x = at_x;		break;
    case RIGHT: at_x -= width - 1;	break;
    case CENTER: at_x -= width/2;	break;
    }

    switch (alignment_y) {
    case TOP:	 at_y = at_y;		break;
    case BOTTOM: at_y += height - 1;	break;
    case CENTER: at_y -= height/2;	break;
    }

    /* Draw text at that position */
    gui_lock();
    for (x=0; text[x]; x++) {
	char c = text[x];
	if (isdigit(c)) {
	    int row, col;
	    int digit = c - '0';
	    /* Paint the character */
	    for (col = 0; col<3; col++) {
	        for (row = 0; row<5; row++) {
		   gui_putpixel(at_x + col, at_y - row,
				(char *)(digits[10*row + digit][col] == '0'
					 ? &green : &black));
		}
	    }
	    /* Paint the inter-character gap if there's another character */
	    if (text[x+1])
	        for (row = 0; row<5; row++)
		   gui_putpixel(at_x + 3, at_y - row, (char *)&black);
	    at_x += 4;
	}
    }
    gui_unlock();
}
