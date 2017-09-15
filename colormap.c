/*
 * colormap.c - Everything to do with mapping magnitude values to colors.
 */

#include <stdlib.h>	/* for exit() */
#include <stdio.h>	/* for fprintf(stderr, ...) */
#include <assert.h>
#include <math.h>

#include "spettro.h"

/* These values were originally calculated for a dynamic range of 180dB. */
static unsigned char map[][3] = {
    { 255, 255,	255 },	/* -0dB */
    { 240, 254,	216 },	/* -10dB */
    { 242, 251,	185 },	/* -20dB */
    { 253, 245,	143 },	/* -30dB */
    { 253, 200,	102 },	/* -40dB */
    { 252, 144,	66  },	/* -50dB */
    { 252,  75,	32  },	/* -60dB */
    { 237,  28,	41  },	/* -70dB */
    { 214,   3,	64  },	/* -80dB */
    { 183,   3,	101 },	/* -90dB */
    { 157,   3,	122 },	/* -100dB */
    { 122,   3,	126 },	/* -110dB */
    {  80,   2,	110 },	/* -120dB */
    {  45,   2,	89  },	/* -130dB */
    {  19,   2,	70  },	/* -140dB */
    {   1,   3,	53  },	/* -150dB */
    {   1,   3,	37  },	/* -160dB */
    {   1,   2,	19  },	/* -170dB */
    {   0,   0,	0   },	/* -180dB */
};

/*
 * Function colormap() is a CPU hotspot because it is called for every
 * newly-painted pixel so we precalculate a long color table from the
 * 20 or so control points in the heat map, so that interpolation between
 * the entries is only done once.
 *
 * A table of 256 values gives equal or finer in resolution than the
 * 8-bit color value it returns.
 */
#define COLOR_MAP_SIZE 256
static unsigned char color_map[COLOR_MAP_SIZE][4];

static void
precompute_color_map(double min_db)
{
    double value;	/* index into map as a floating point value */
    int indx;		/* Index into colormap */
    double rem;		/* Mantissa of indx */
    int indx2;		/* Index into color_map */

    for (indx2=0; indx2 < COLOR_MAP_SIZE; indx2++) {
	/* color_map[0-255] represent 0.0 to min_db decibels;
	 * map[0-18] represent 0.0 to min_db decibels
	 */
	value = (double)((sizeof(map)/sizeof(map[0]) - 1) * indx2) / (COLOR_MAP_SIZE - 1);
	indx = floor(value);
	if (indx < 0) {
	    fprintf(stderr, "colormap: array index is %d\n", indx);
	    exit(1);
	}

	/* Use mantissa of value to interpolate between color values */
	rem = fmod(value, 1.0);	/* 0.0 <= rem < 1.0 */

	/* The map is R,G,B while color[] is [B,G,R]
	 * (to match ARGB on a little-endian machine)
	 */
	color_map[indx2][3] = 0xFF;
	color_map[indx2][2] = lrintf((1.0-rem)*map[indx][0]+rem*map[indx+1][0]);
	color_map[indx2][1] = lrintf((1.0-rem)*map[indx][1]+rem*map[indx+1][1]);
	color_map[indx2][0] = lrintf((1.0-rem)*map[indx][2]+rem*map[indx+1][2]);
    }
}

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
    float rem;
    int indx;
    static bool precomputed = FALSE;

    if (gray_scale) {
     	/* "value" is a negative value in decibels.
    	 * black (0,0,0) is for <= -180.0, and the other 255 values
    	 * should cover the range from -180 to 0 evenly.
    	 * (value/min_db) is >=0.0  and <1.0
    	 * because both value and min_db are negative.
    	 * (v/s) * 255.0 goes from 0.0 to 254.9999999 and
    	 * floor((v/s) * 255) gives us 0 to 254
    	 * converted to 255 to 1 by subtracting it from 255.
	 */
    	int gray; /* The pixel value */

    	if (value <= min_db) {
    		gray = 0;
    	} else {
    		gray = 255 - lrint(floor((value / min_db) * 255.0));
    		assert(gray >= 1 && gray <= 255);
	}
    	color[0] = color[1] = color[2] = gray;
    	return;
    }

    if (!precomputed) {
	precompute_color_map(min_db);
	precomputed = TRUE;
    }

    /* Convert value into the index into the precomputed color table */
    indx = lrint(value / min_db * COLOR_MAP_SIZE);

    if (value >= 0.0) {
	color[0] = color[1] = color[2] = 255;
	return;
    }

    if (value <= min_db || indx > 255) {
	color[0] = color[1] = color[2] = 0;
	return;
    }

#if LITTLE_ENDIAN	/* Provided by stdlib.h on Linux-glibc */
    *(unsigned long *)color = *(unsigned long *)(color_map[indx]);
#else
    color[0] = color_map[indx][0];
    color[1] = color_map[indx][1];
    color[2] = color_map[indx][2];
    color[3] = 0xFF;
#endif

    return;
}
