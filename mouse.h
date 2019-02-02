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
 * mouse.h - Declarations for mouse.c
 */

/* Our names for the buttons in mouse-down/mouse-up events */
typedef enum mouse_button {
    LEFT_BUTTON,
    RIGHT_BUTTON,
} mouse_button_t;

extern void do_mouse_button(unsigned screen_x, unsigned screen_y,
			    mouse_button_t button, bool down);
/* Literal values for "down" parameter */
#define MOUSE_UP	FALSE
#define MOUSE_DOWN	TRUE

extern void do_mouse_move(int screen_x, int screen_y);

#if EVAS_VIDEO
#include <Evas.h>
extern void mouseDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
extern void mouseUp(void *data, Evas *e, Evas_Object *obj, void *event_info);
extern void mouseMove(void *data, Evas *e, Evas_Object *obj, void *event_info);
#endif
