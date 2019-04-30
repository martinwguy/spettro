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

static char *lettersAG[] = {
    " 0 ", "00 ", " 00", "00 ", "000", "000", " 00",
    "0 0", "0 0", "0  ", "0 0", "0  ", "0  ", "0  ",
    "0 0", "00 ", "0  ", "0 0", "00 ", "00 ", "0 0",
    "000", "0 0", "0  ", "0 0", "0  ", "0  ", "0 0",
    "0 0", "00 ", " 00", "00 ", "000", "0  ", " 0 ",
};
static const int AG_stride = 7;
static char *lettersHQ[] = {
    "0 0", "000", "000", "0 0", "0  ", "0 0", "   ", " 0 ", "00 ", " 0 ",
    "0 0", " 0 ", "  0", "00 ", "0  ", "000", "00 ", "0 0", "0 0", "0 0",
    "000", " 0 ", "  0", "0  ", "0  ", "0 0", "0 0", "0 0", "00 ", "0 0",
    "0 0", " 0 ", "  0", "00 ", "0  ", "0 0", "0 0", "0 0", "0  ", "0 0",
    "0 0", "000", "00 ", "0 0", "000", "0 0", "0 0", " 0 ", "0  ", " 00",
};
static const int HQ_stride = 10;
static char *lettersRZ[] = {
    "00 ", " 00", "000", "0 0", "0 0", "0 0", "0 0", "0 0", "000",
    "0 0", "0  ", " 0 ", "0 0", "0 0", "0 0", "0 0", "0 0", "  0",
    "00 ", " 0 ", " 0 ", "0 0", "0 0", "0 0", " 0 ", " 0 ", " 0 ",
    "0 0", "  0", " 0 ", "0 0", "0 0", "000", "0 0", " 0 ", "0  ",
    "0 0", "00 ", " 0 ", "000", " 0 ", "0 0", "0 0", "0  ", "000",
};
static const int RZ_stride = 9;

/*
 * Return the width of a text in pixels
 */
int
text_width(const char *text)
{
    /* Calculate the width of the typeset string in pixels */
    int width = 0;
    int x;

    for (x = 0; text[x] != '\0'; x++) {
    	char c = toupper(text[x]);
	if (isdigit(c) || isupper(c)) width += 4;
	else if (c == '.') width += 2;
	else if (c == ':') width += 2;
	else if (c == '+') width += 4;
	else if (c == '-') width += 4;
	else if (c == '=') width += 4;
	else if (c == ' ') width += 2;
    }
    if (width > 0) width--; /* Not including the trailing blank column */

    return width;
}
    
/*
 * Draw the given text at the given coordinates.
 * Alignment TOP put the top pixel of the text at that y coordinat
 * Alignment LEFT puts the leftmost pixel of the text at that x coordinat
 * CENTER centers the text on that coordinate value.
 *
 * gui_lock() and gui_unlock() must be placed around calls to this.
 */
void
draw_text(const char *text, int at_x, int at_y,
		      alignment_t alignment_x, alignment_t alignment_y)
{
    int width = text_width(text);
    int height = 5;
    int x;	/* index into the string */

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
    for (x=0; text[x]; x++) {
	char c = toupper(text[x]);

	if (c == '.') {
	    gui_putpixel(at_x, at_y - 4, green);
	    at_x += 2;
	} else if (c == ':') {
	    gui_putpixel(at_x, at_y - 3, green);
	    gui_putpixel(at_x, at_y - 1, green);
	    at_x += 2;
	} else if (c == '+') {
	    gui_putpixel(at_x+1, at_y - 1, green);	/* top */
	    gui_putpixel(at_x,   at_y - 2, green);	
	    gui_putpixel(at_x+1, at_y - 2, green);
	    gui_putpixel(at_x+2, at_y - 2, green);
	    gui_putpixel(at_x+1, at_y - 3, green);	/* bottom */
	    at_x += 4;
	} else if (c == '-') {
	    gui_putpixel(at_x,   at_y - 2, green);	
	    gui_putpixel(at_x+1, at_y - 2, green);
	    gui_putpixel(at_x+2, at_y - 2, green);
	    at_x += 4;
	} else if (c == '=') {
	    gui_putpixel(at_x,   at_y - 1, green);	
	    gui_putpixel(at_x+1, at_y - 1, green);
	    gui_putpixel(at_x+2, at_y - 1, green);
	    gui_putpixel(at_x,   at_y - 3, green);	
	    gui_putpixel(at_x+1, at_y - 3, green);
	    gui_putpixel(at_x+2, at_y - 3, green);
	    at_x += 4;
	} else if (c == ' ') {
	    at_x += 2;
	} else if (isdigit(c) || isupper(c)) {
	    char **glyphs;
	    int stride;
	    int row, col;
	    int digit;

	    if (isdigit(c)) {
		glyphs = digits;
		stride = digit_stride;
		digit = c - '0';
	    } else if (c >= 'A' && c <= 'G') {
		glyphs = lettersAG;
		stride = AG_stride;
		digit = c - 'A';
	    } else if (c >= 'H' && c <= 'Q') {
		glyphs = lettersHQ;
		stride = HQ_stride;
		digit = c - 'H';
	    } else if (c >= 'R' && c <= 'Z') {
		glyphs = lettersRZ;
		stride = RZ_stride;
		digit = c - 'R';
	    } else {
	    	fprintf(stderr, "Unknown text character '%c'\n", c);
    		gui_unlock();
		return;
	    }
	    /* Paint the character */
	    for (col = 0; col<3; col++)
	        for (row = 0; row<5; row++)
		   if (glyphs[stride*row + digit][col] == '0')
		       gui_putpixel(at_x + col, at_y - row, green);

	    at_x += 4;
	}
    }
}
