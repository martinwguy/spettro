/*
 * mouse.c - Mouse-handling code
 */

#include "spettro.h"

#include "mouse.h"

#include "key.h"	/* for Shift and Control */
#include "overlay.h"	/* for set_bar_*_time() */
#include "ui_funcs.h"
#include "main.h"

#include <math.h>

/*
 * They can click and release the mouse of the same point.
 * If Control, this means o set ehe left/right bar lines.
 *
 * If they move the mouse between Control-clock and contol-release,
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
    Control = evas_key_modifier_is_set(modifiers, "Control");

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
    Control = evas_key_modifier_is_set(modifiers, "Control");
fprintf(stderr, "Evas mouse move\n");

    do_mouse_move(ev->cur.canvas.x, ev->cur.canvas.y);
}
#endif

/*
 * Process a mouse button click or release and mouse movements
 */
void
do_mouse_button(unsigned screen_x, unsigned screen_y, mouse_button_t button, bool down)
{
    double when = disp_time + (screen_x - disp_offset) * step;

    if (down) {
	/* For mouse drag, Remember where it went down and what
	 * key modifiers were held at the time */
	mouse_down_x = screen_x;
	mouse_down_y = screen_y;
	mouse_down_shift = Shift;
	mouse_down_ctrl = Control;

	switch (button) {
	    case LEFT_BUTTON:	left_button_is_down = TRUE;	break;
	    case RIGHT_BUTTON:	right_button_is_down = TRUE;	break;
	}
    }

    /* Mouse up while setting bar line position: set it. */
    if (!down && Control) switch (button) {
    case LEFT_BUTTON:	set_bar_left_time(when);	break;
    case RIGHT_BUTTON:	set_bar_right_time(when);	break;
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
    if (Control) {
	double when = disp_time + (screen_x - disp_offset) * step;

	if (left_button_is_down) set_bar_left_time(when);
	if (right_button_is_down) set_bar_right_time(when);
    }

    /* Plain dragging while holding left button:
     * pan the display by N pixels */
    if (!Control && !Shift && left_button_is_down) {
	if (screen_x != mouse_down_x) {
	    time_pan_by((mouse_down_x - screen_x) * step);
	}
	if (screen_y != mouse_down_y) {
	    double one_pixel = exp(log(max_freq/min_freq) / (disp_height-1));
	    freq_pan_by(pow(one_pixel, (double)(screen_y - mouse_down_y)));
	}
	if (screen_x != mouse_down_x || screen_y != mouse_down_y)
	    repaint_display();
    }
    /* This isn't right, but works with the current logic */
    mouse_down_x = screen_x;
    mouse_down_y = screen_y;
}
