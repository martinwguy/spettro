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
 * mouse.c - Mouse-handling code
 */

#include "spettro.h"

#include "mouse.h"

#include "barlines.h"
#include "convert.h"
#include "key.h"	/* for Shift and Ctrl */
#include "overlay.h"
#include "paint.h"
#include "ui_funcs.h"
#include "ui.h"


/*
 * They can click and release the mouse of the same point.
 * If Ctrl, this means o set ehe left/right bar lines.
 *
 * If they move the mouse between Ctrl-clock and contol-release,
 * this means to place/reposition the left/right bar line marker.
 *
 * If they move the mouse while holding a button down without ctrl,
 * this is a pan command, by frequency and by time.
 */

/* Remember where they clicked down and which modifier keys were held */
static int mouse_down_x, mouse_down_y;
static int mouse_down_shift = 0;;
static int mouse_down_ctrl = 0;;
static bool left_button_is_down = FALSE;
static bool right_button_is_down = FALSE;

#if ECORE_MAIN

#include <Ecore.h>

static void
mouseAnything(void *data, Evas *evas, Evas_Object *obj, void *einfo, bool down);

void
mouseDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    mouseAnything(data, evas, obj, einfo, MOUSE_DOWN);
}

void
mouseUp(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    mouseAnything(data, evas, obj, einfo, MOUSE_UP);
}

static void
mouseAnything(void *data, Evas *evas, Evas_Object *obj, void *einfo, bool down)
{
    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Coord_Point *where = &(ev->canvas);
    Evas_Modifier *modifiers = ev->modifiers;

    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Ctrl = evas_key_modifier_is_set(modifiers, "Ctrl");

    if (down) {
	mouse_down_x = where->x;
	mouse_down_y = where->y;
	switch (ev->button) {
	case 1: left_button_is_down = TRUE; break;
	case 3: right_button_is_down = TRUE; break;
	}
    } else /* Up */
	if (where->x != mouse_down_x || where->y != mouse_down_y)
	    do_mouse_move(where->x, where->y);

    switch (ev->button) {
    case 1: do_mouse_button(where->x, where->y, LEFT_BUTTON, down); break;
    case 3: do_mouse_button(where->x, where->y, RIGHT_BUTTON, down); break;
    }
}

void
mouseMove(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Mouse_Move *ev = einfo;
    Evas_Modifier *modifiers = ev->modifiers;
    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Ctrl = evas_key_modifier_is_set(modifiers, "Ctrl");

    do_mouse_move(ev->cur.canvas.x, ev->cur.canvas.y);
}
#endif

/*
 * Process a mouse button click or release and mouse movements
 *
 * "down" is TRUE if this is a mouse-down event, FALSE if a mouse up.
 */
void
do_mouse_button(unsigned screen_x, unsigned screen_y, mouse_button_t button, bool down)
{
    double when = screen_column_to_start_time(screen_x);

    if (down) {
	/* For mouse drag, Remember where it went down and what
	 * key modifiers were held at the time */
	mouse_down_x = screen_x;
	mouse_down_y = screen_y;
	mouse_down_shift = Shift;
	mouse_down_ctrl = Ctrl;

	switch (button) {
	    case LEFT_BUTTON:	left_button_is_down = TRUE;	break;
	    case RIGHT_BUTTON:	right_button_is_down = TRUE;	break;
	}
    }

    /* Mouse up while setting bar line position: set it. */
    if (down && Ctrl) switch (button) {
    case LEFT_BUTTON:	set_left_bar_time(when);	break;
    case RIGHT_BUTTON:	set_right_bar_time(when);	break;
    }

    if (!down) switch (button) {
	case LEFT_BUTTON:	left_button_is_down = FALSE;	break;
	case RIGHT_BUTTON:	right_button_is_down = FALSE;	break;
    }
}

void
do_mouse_move(int screen_x, int screen_y)
{
    /* Dragging the mouse left/right while setting a bar line */
    if (Ctrl) {
	double when = screen_column_to_start_time(screen_x);

	if (left_button_is_down) set_left_bar_time(when);
	if (right_button_is_down) set_right_bar_time(when);
    }

    /* Plain dragging while holding left button:
     * pan the display by N pixels */
    if (!Ctrl && !Shift && left_button_is_down) {
	if (screen_x != mouse_down_x) {
	    time_pan_by((mouse_down_x - screen_x) * step);
	}
	if (screen_y != mouse_down_y) {
	    double one_pixel = exp(log(max_freq/min_freq) / (disp_height-1));
	    freq_pan_by(pow(one_pixel, (double)(screen_y - mouse_down_y)));
	}
	if (screen_x != mouse_down_x || screen_y != mouse_down_y)
	    repaint_display(TRUE);
    }
    /* This isn't right, but works with the current logic */
    mouse_down_x = screen_x;
    mouse_down_y = screen_y;
}
