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

/* window.h: Declarations for window.c */

#ifndef WINDOW_H

typedef enum {
    ANY_WINDOW = -1, /* Used to see if a column has any results in the cache */
    KAISER = 0,
    DOLPH,
    NUTTALL,
    BLACKMAN,
    HANN,
    NUMBER_OF_WINDOW_FUNCTIONS
} window_function_t;

extern float *get_window(window_function_t wfunc, int datalen);
extern void free_windows(void);
extern const char *window_name(window_function_t wfunc);
extern const char window_key(window_function_t wfunc);

#define WINDOW_H
#endif
