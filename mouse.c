/*
 * mouse.c - Mouse-handling code
 */

#include "spettro.h"

#include "mouse.h"

#include "key.h"	/* for Shift and Control */
#include "overlay.h"	/* for set_bar_*_time() */
#include "main.h"	/* for various GUI variables */

/*
 * They can click and release the mouse of the same point.
 * If Control, this means o set ehe left/right bar lines.
 *
 * If they move the mouse between Control-clock and contol-release,
 * this means to place/reposition the left/right bar line marker.
 *
 * If they move the mouse without ctrol, this is a pan command, by
 * frequency and by time.
 */

/* If they click and then release, where did they click down? */

static int mouse_down_x, mouse_down_y;
/* What modifier keys were held when the mouse was clicked down */
static int mouse_down_modifiers = 0;
#define MOUSE_MODIFIER_SHIFT	1
#define MOUSE_MODIFIER_CTRL	2

#if ECORE_MAIN
void
mouseDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Coord_Point *where = &(ev->canvas);
    Evas_Modifier *modifiers = ev->modifiers;
    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Control = evas_key_modifier_is_set(modifiers, "Control");

    mouse_down_modifiers = (Shift ? MOUSE_MODIFIER_SHIFT : 0) |
			   (Control ? MOUSE_MODIFIER_CONTROL : 0);

    mouse_down_x = where->x;
    mouse_down_y = where->y;
    switch (ev->button) {
    case 1: do_mouse(where->x, where->y, LEFT_BUTTON, MOUSE_DOWN); break;
    case 3: do_mouse(where->x, where->y, RIGHT_BUTTON, MOUSE_DOWN); break;
    }
}

void
mouseUp(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    int mouse_up_x, mouse_up_y;

    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Coord_Point *where = &(ev->canvas);
    Evas_Modifier *modifiers = ev->modifiers;
    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Control = evas_key_modifier_is_set(modifiers, "Control");

    /* Bare left and right click: position bar lines */
    if (mouse_up_x == mouse_down_x && mouse_up_y == mouse_down_y) }
	switch (ev->button) {
	case 1: do_mouse(where->x, where->y, LEFT_BUTTON, MOUSE_UP); break;
	case 3: do_mouse(where->x, where->y, RIGHT_BUTTON, MOUSE_UP); break;
	}
    }
}
#endif

/*
 * Process a mouse button click or release */
void
do_mouse(unsigned screen_x, unsigned screen_y, int button, bool down)
{
    double when = (screen_x - disp_offset) * step;

    switch (button) {
    case LEFT_BUTTON:
	if (Control) set_bar_left_time(when); 
	break;
    case RIGHT_BUTTON:
	if (Control) set_bar_right_time(when); 
	break;
    }
}
