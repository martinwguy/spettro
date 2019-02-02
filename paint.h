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
 * paint.h: Declarations for paint.c
 */

#ifndef PAINT_H

#include "calc.h"		/* for result_t */

extern void do_scroll(void);
extern void repaint_display(bool repaint_all);
extern void repaint_columns(int from_x, int to_x, int from_y, int to_y, bool refresh_only);
extern void repaint_column(int column, int min_y, int max_y, bool refresh_only);
extern void paint_column(int pos_x, int min_y, int max_y, result_t *result);

#define PAINT_H
#endif
