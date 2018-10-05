/*
 * colormap.c - Everything to do with mapping magnitude values to colors.
 */

#include <stdlib.h>	/* for exit() */
#include <stdio.h>	/* for fprintf(stderr, ...) */
#include <math.h>

#include "spettro.h"

/*
 * Map a magnitude value to a color.
 *
 * "value" is a negative value in decibels, with maximum of 0.0.
 * "min_db" is the negative decibel value for the bottom of the color range.
 * The resulting color is deposited in color[B,G,R].
 */
void
colormap(double value, double min_db, unsigned char *color, bool gray_scale)
{
    /* These values were originally calculated for a dynamic range of 180dB. */
    static unsigned char map[][3] = {
    	{ 255,   255,	255	},	/* -0dB */
    	{ 240,   254,	216	},	/* -10dB */
    	{ 242,   251,	185	},	/* -20dB */
    	{ 253,   245,	143	},	/* -30dB */
    	{ 253,   200,	102	},	/* -40dB */
    	{ 252,   144,	66	},	/* -50dB */
    	{ 252,   75,	32	},	/* -60dB */
    	{ 237,   28,	41	},	/* -70dB */
    	{ 214,   3,	64	},	/* -80dB */
    	{ 183,   3,	101	},	/* -90dB */
    	{ 157,   3,	122	},	/* -100dB */
    	{ 122,   3,	126	},	/* -110dB */
    	{ 80,	 2,	110	},	/* -120dB */
    	{ 45,	 2,	89	},	/* -130dB */
    	{ 19,	 2,	70	},	/* -140dB */
    	{ 1,	 3,	53	},	/* -150dB */
    	{ 1,	 3,	37	},	/* -160dB */
    	{ 1,	 2,	19	},	/* -170dB */
    	{ 0,	 0,	0	},	/* -180dB */
    };

    float rem;
    int indx;

    if (gray_scale) {
    	int gray; /* The pixel value */

    	if (value <= min_db) {
	    gray = 0;
    	} else {
	    /* "value" is a negative value in decibels.
	     * black (0,0,0) is for <= -180.0, and the other 255 values
	     * should cover the range from -180 to 0 evenly.
	     * (value/min_db) is >=0.0  and <1.0
	     * because both value and min_db are negative.
	     * (v/s) * 255.0 goes from 0.0 to 254.9999999 and
	     * floor((v/s) * 255) gives us 0 to 254
	     * converted to 255 to 1 by subtracting it from 255.
	     */
	    gray = 255 - lrint(floor((value / min_db) * 255.0));
	}
    	color[0] = color[1] = color[2] = gray;
    	return;
    }

    if (value >= 0.0) {
	color[0] = color[1] = color[2] = 255;
	return;
    }

    if (value <= min_db) {
	color[0] = color[1] = color[2] = 0;
	return;
    }

    value = fabs(value * (-180.0 / min_db) * 0.1);

    indx = lrintf(floor(value));

    if (indx < 0) {
	fprintf(stderr, "colormap: array index is %d.\n", indx);
	/* Carry on with the show */
	return;
    }

    if (indx >= sizeof(map)/sizeof(*map) - 1) {
	color[0] = color[1] = color[2] = 0;
	return;
    }

    rem = fmod(value, 1.0);

    /* The map is R,G,B while color[] is [B,G,R] (to match ARGB on a little-endian machine) */
    color[2] = lrintf((1.0 - rem) * map[indx][0] + rem * map[indx + 1][0]);
    color[1] = lrintf((1.0 - rem) * map[indx][1] + rem * map[indx + 1][1]);
    color[0] = lrintf((1.0 - rem) * map[indx][2] + rem * map[indx + 1][2]);

    return;
}
