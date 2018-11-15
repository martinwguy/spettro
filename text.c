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
static const int digit_stride = 10;

static char *letters[] = {
    " 0 ", "00 ", " 00", "00 ", "000", "000", " 00",
    "0 0", "0 0", "0  ", "0 0", "0  ", "0  ", "0  ",
    "0 0", "00 ", "0  ", "0 0", "00 ", "00 ", "0 0",
    "000", "0 0", "0  ", "0 0", "0  ", "0  ", "0 0",
    "0 0", "00 ", " 00", "00 ", "000", "0  ", " 0 ",
};
static const int letter_stride = 7;

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
    int width;
    int height = 5;
    int x;	/* index into the string */

    /* Calculate the width of the typeset string in pixels */
    width = 0;
    for (x = 0; text[x] != '\0'; x++) {
	if (isdigit(text[x]) || isupper(text[x])) width += 4;
	else if (text[x] == '.') width += 2;
    }
    if (width > 0) width--; /* Not including the trailing blank column */

    /* Make at_x and at_y the position of the top left pixel of the text */
    switch (alignment_x) {
    case LEFT:	/* at_x = at_x;	*/	break;
    case RIGHT: at_x -= width - 1;	break;
    case CENTER: at_x -= width/2;	break;
    }

    switch (alignment_y) {
    case TOP:	 /* at_y = at_y; */	break;
    case BOTTOM: at_y += height - 1;	break;
    case CENTER: at_y += height/2;	break;
    }

    /* Draw text at that position */
    gui_lock();
    for (x=0; text[x]; x++) {
	char c = text[x];
	if (isdigit(c) || isupper(c)) {
	    char **glyphs;
	    int stride;
	    int row, col;
	    int digit;

	    if (isdigit(c)) {
		glyphs = digits;
		stride = digit_stride;
		digit = c - '0';
	    } else {
		glyphs = letters;
		stride = letter_stride;
		digit = c - 'A';
	    }
	    /* Paint the character */
	    for (col = 0; col<3; col++) {
	        for (row = 0; row<5; row++) {
		   gui_putpixel(at_x + col, at_y - row,
				glyphs[stride*row + digit][col] == '0'
				? green : black);
		}
	    }
	    /* Paint the inter-character gap if there's another character */
	    if (text[x+1])
	        for (row = 0; row<5; row++)
		   gui_putpixel(at_x + 3, at_y - row, black);
	    at_x += 4;
	} else if (c == '.') {
	    gui_paint_rect(at_x, at_y - 4, at_x+1, at_y, black);
	    gui_putpixel(at_x, at_y - 4, green);
	    at_x += 2;
	}
    }
    gui_unlock();
}
