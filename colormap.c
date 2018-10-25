/*
 * colormap.c - Everything to do with mapping magnitude values to colors.
 */

#include <stdlib.h>	/* for exit() */
#include <stdio.h>	/* for fprintf(stderr, ...) */
#include <math.h>

#include "spettro.h"
#include "colormap.h"

/* Heat map from sndfile-spectrogram */
/* These values were originally calculated for a dynamic range of 180dB. */
static unsigned char sndfile_map[][3] = {
    { 255, 255, 255 },	/* -0dB */
    { 240, 254, 216 },	/* -10dB */
    { 242, 251, 185 },	/* -20dB */
    { 253, 245, 143 },	/* -30dB */
    { 253, 200, 102 },	/* -40dB */
    { 252, 144, 66  },	/* -50dB */
    { 252, 75,  32  },	/* -60dB */
    { 237, 28,  41  },	/* -70dB */
    { 214, 3,   64  },	/* -80dB */
    { 183, 3,   101 },	/* -90dB */
    { 157, 3,   122 },	/* -100dB */
    { 122, 3,   126 },	/* -110dB */
    { 80,  2,   110 },	/* -120dB */
    { 45,  2,   89  },	/* -130dB */
    { 19,  2,   70  },	/* -140dB */
    { 1,   3,   53  },	/* -150dB */
    { 1,   3,   37  },	/* -160dB */
    { 1,   2,   19  },	/* -170dB */
    { 0,   0,   0   },	/* -180dB */
};
#define sndfile_map_len (sizeof(sndfile_map) / sizeof(sndfile_map[0]))

static unsigned char sox_map[][3] = {
    {242,255,235}, {242,255,232}, {241,255,230}, {241,255,228},
    {241,255,225}, {241,255,223}, {241,255,221}, {240,254,218},
    {240,254,216}, {240,254,214}, {240,254,212}, {240,254,209},
    {239,254,207}, {239,254,204}, {240,254,201}, {240,253,197},
    {241,253,192}, {243,252,188}, {244,251,183}, {245,250,179},
    {246,250,174}, {247,249,170}, {248,249,165}, {249,248,161},
    {250,247,156}, {251,247,152}, {252,246,147}, {253,245,143},
    {254,243,139}, {254,238,134}, {254,233,130}, {254,228,126},
    {254,223,122}, {254,218,118}, {254,213,113}, {253,208,109},
    {253,203,105}, {253,198,101}, {253,193, 97}, {253,188, 92},
    {253,183, 88}, {253,177, 84}, {253,172, 80}, {253,165, 77},
    {253,158, 73}, {253,150, 69}, {253,143, 66}, {253,136, 62},
    {253,129, 59}, {252,122, 55}, {252,114, 51}, {252,107, 48},
    {252,100, 44}, {252, 93, 42}, {252, 86, 38}, {252, 79, 34},
    {252, 73, 32}, {250, 67, 32}, {248, 62, 33}, {246, 57, 35},
    {245, 52, 35}, {244, 47, 36}, {242, 42, 38}, {240, 38, 39},
    {238, 32, 40}, {237, 27, 41}, {236, 23, 42}, {234, 17, 43},
    {232, 13, 44}, {230,  7, 45}, {228,  3, 47}, {226,  3, 50},
    {222,  3, 54}, {219,  3, 58}, {215,  3, 62}, {212,  3, 67},
    {209,  3, 70}, {205,  3, 74}, {202,  3, 78}, {199,  3, 82},
    {195,  3, 86}, {192,  3, 90}, {188,  3, 94}, {186,  3, 98},
    {182,  3,102}, {179,  3,104}, {177,  3,106}, {174,  3,109},
    {171,  3,111}, {169,  3,113}, {166,  3,115}, {163,  3,117},
    {160,  3,120}, {158,  3,122}, {155,  3,124}, {152,  3,126},
    {150,  3,128}, {147,  3,131}, {144,  3,133}, {140,  3,132},
    {135,  3,130}, {131,  3,128}, {126,  3,126}, {122,  3,125},
    {117,  3,124}, {112,  3,122}, {108,  2,120}, {104,  2,118},
    { 99,  2,117}, { 95,  2,116}, { 90,  2,114}, { 85,  2,112},
    { 81,  2,110}, { 77,  2,108}, { 73,  2,106}, { 69,  2,104},
    { 66,  2,101}, { 62,  2, 99}, { 59,  2, 97}, { 55,  2, 95},
    { 51,  2, 92}, { 48,  2, 90}, { 43,  2, 87}, { 40,  2, 85},
    { 36,  2, 83}, { 32,  2, 81}, { 29,  2, 79}, { 26,  2, 77},
    { 24,  2, 75}, { 23,  2, 74}, { 21,  2, 72}, { 19,  2, 70},
    { 17,  2, 68}, { 15,  2, 66}, { 14,  3, 65}, { 11,  3, 63},
    {  9,  3, 61}, {  7,  3, 59}, {  5,  3, 57}, {  4,  3, 56},
    {  2,  3, 54}, {  1,  3, 52}, {  1,  3, 50}, {  1,  3, 48},
    {  1,  3, 47}, {  1,  3, 46}, {  1,  3, 44}, {  1,  3, 42},
    {  1,  3, 40}, {  1,  3, 39}, {  1,  3, 37}, {  1,  3, 36},
    {  1,  3, 34}, {  1,  3, 32}, {  1,  3, 31}, {  1,  3, 29},
    {  1,  3, 27}, {  1,  2, 24}, {  1,  2, 22}, {  1,  2, 21},
    {  1,  2, 18}, {  1,  2, 16}, {  0,  1, 14}, {  0,  1, 11},
    {  0,  1, 10}, {  0,  1,  8}, {  0,  1,  5}, {  0,  0,  3},
    {  0,  0,  0}, 
};
#define sox_map_len (sizeof(sox_map) / sizeof(sox_map[0]))

/* White marks on a black background */
static unsigned char gray_map[][3] = {
    { 255, 255, 255 },	/* -0dB */
    {   0,   0,   0 },  /* min_db */
};
#define gray_map_len (sizeof(gray_map) / sizeof(gray_map[0]))

/* Black marks on a white background */
static unsigned char print_map[][3] = {
    {   0,   0,   0 },	/* -0dB */
    { 255, 255, 255 },  /* min_db */
};
#define print_map_len (sizeof(print_map) / sizeof(print_map[0]))

static unsigned char (*map)[3] = sox_map;
static int map_len = sox_map_len;

/* Which color map do they want? */
static int which = 0;

void
set_colormap(int w)
{
    switch (which = w) {
    case 0: map = sox_map;	map_len = sox_map_len;		break;
    case 1: map = sndfile_map;	map_len = sndfile_map_len; 	break;
    case 2: map = gray_map;	map_len = gray_map_len;		break;
    case 3: map = print_map;	map_len = print_map_len;	break;
    }
}

void
change_colormap()
{
    set_colormap((which + 1) % NMAPS);
}

/*
 * Map a magnitude value to a color.
 *
 * "value" is a negative value in decibels, with maximum of 0.0.
 * "min_db" is the negative decibel value for the bottom of the color range.
 * The resulting color is deposited in color[B,G,R].
 */
void
colormap(double value, double min_db, unsigned char *color)
{
    double rem;
    double findx;
    int indx;

    if (value >= 0.0) {
	color[0] = map[0][0];
	color[1] = map[0][1];
	color[2] = map[0][2];
	return;
    }

    if (value <= min_db) {
	color[0] = map[map_len-1][0];
	color[1] = map[map_len-1][1];
	color[2] = map[map_len-1][2];
	return;
    }
    
    /* floating-point version of indx */
    findx = value * (map_len-1) / min_db;
    indx = lrintf(floor(findx));
    rem = fmod(findx, 1.0);

    if (indx < 0) {
	fprintf(stderr, "colormap: array index is %d because value is %g.\n",
		indx, value);
	/* Carry on with the show */
	return;
    }

    if (indx > map_len - 2) {	/* Need map[indx] and map[indx+1] */
	color[0] = color[1] = color[2] = 0;
	return;
    }

    /* The map is R,G,B while color[] is [B,G,R] (to match ARGB on a little-endian machine) */
    color[2] = lrintf((1.0 - rem) * map[indx][0] + rem * map[indx + 1][0]);
    color[1] = lrintf((1.0 - rem) * map[indx][1] + rem * map[indx + 1][1]);
    color[0] = lrintf((1.0 - rem) * map[indx][2] + rem * map[indx + 1][2]);

    return;
}
