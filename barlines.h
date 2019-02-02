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

/* barlines.h: Declarations for barlines.c */

#include "gui.h"	/* for color_t */

/* Value for when a bar line is not set */
#define UNDEFINED (-1.0)

extern void set_left_bar_time(double when);
extern void set_right_bar_time(double when);
extern void set_beats_per_bar(int bpb);
extern double get_left_bar_time(void);
extern double get_right_bar_time(void);
extern int get_beats_per_bar(void);
extern bool get_col_overlay(int x, color_t *colorp);
