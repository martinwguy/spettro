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

/*
 * colormap.c - Everything to do with mapping magnitude values to colors.
 */

#include "spettro.h"
#include "colormap.h"

#include "gui.h"
#include "ui.h"		/* for dyn_range */


/* Which elements of *_map[] represent which primary colors? */
#define R 0
#define G 1
#define B 2

/* The type of the value for one primary color */
typedef unsigned char primary_t;

/* color maps run from the RGB color of the brightest value (0.0) to
 * the darkest color for a value of -dyn_range */

/* Heatmap from sox spectrogram */
static primary_t sox_map[][3] = {
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
static primary_t gray_map[][3] = {
    { 255, 255, 255 },	/* -0dB */
    {   0,   0,   0 },  /* -dyn_range */
};
#define gray_map_len (sizeof(gray_map) / sizeof(gray_map[0]))

/* Black marks on a white background */
static primary_t print_map[][3] = {
    {   0,   0,   0 },	/* -0dB */
    { 255, 255, 255 },  /* -dyn_range */
};
#define print_map_len (sizeof(print_map) / sizeof(print_map[0]))

static primary_t (*map)[3] = sox_map;
static int map_len = sox_map_len;

/* Which color map are we using? */
static int which = HEAT_MAP;

void
set_colormap(int w)
{
    which = w;
    switch (which) {
    case HEAT_MAP:    map = sox_map;	map_len = sox_map_len;    break;
    case GRAY_MAP:    map = gray_map;	map_len = gray_map_len;   break;
    case PRINT_MAP:   map = print_map;	map_len = print_map_len;  break;
    }
}

void
change_colormap()
{
    set_colormap((which + 1) % NUMBER_OF_COLORMAPS);
}

/*
 * Map a magnitude value to a color.
 *
 * "value" is a negative value in decibels, with maximum of 0.0.
 * The decibel value for the bottom of the color range is -dyn_range.
 * Returns the resulting color.
 */
color_t
colormap(float value)
{
    float findx;  /* floating-point version of indx */
    int indx;	/* Index into colormap for a value <= the current one */
    float rem; /* How far does this fall between one index and another
    		 * 0.0 <= rem < 1.0 */
    float min_db = -dyn_range;

    /* Map over-bright values to the brightest color */
    if (DELTA_GE(value, (float)0.0)) return RGB_to_color(map[0][R],
							 map[0][G],
							 map[0][B]);

    /* Map values below the dynamic range to the dimmest color */
    if (DELTA_LE(value, min_db)) return RGB_to_color(map[map_len-1][R],
						     map[map_len-1][G],
						     map[map_len-1][B]);
    
    /* value is < 0.0 and > min_db.
     * Interpolate between elements of the color map.
     */
    findx = value * (map_len-1) / min_db;
    indx = lrintf(floorf(findx));
    rem = fmodf(findx, (float)1.0);

    if (indx < 0) {
	fprintf(stderr, "colormap: array index is %d because value is %g.\n",
		indx, (double)value);
	/* Carry on with the show */
	return gray;
    }

    if (indx > map_len - 2) {	/* Need map[indx] and map[indx+1] */
	return black;
    }

    return RGB_to_color(
        (primary_t)lrintf((1.0f - rem) * map[indx][R] + rem * map[indx + 1][R]),
	(primary_t)lrintf((1.0f - rem) * map[indx][G] + rem * map[indx + 1][G]),
	(primary_t)lrintf((1.0f - rem) * map[indx][B] + rem * map[indx + 1][B]));
}
