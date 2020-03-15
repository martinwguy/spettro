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
 * colormap.h: Header file for colormap.c
 */

#include "gui.h"	/* for color_t */

extern void change_colormap(void);
extern void set_colormap(int which);
extern color_t colormap(float value);

typedef enum {
    HEAT_MAP=0,
    GRAY_MAP,
    PRINT_MAP,
    NUMBER_OF_COLORMAPS
} colormap_t;
