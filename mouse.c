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
 * If they move the mouse while holding a button down without ctrl,
 * this is a pan command, by frequency and by time.
 */

#if ECORE_MAIN
void
mouseDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Coord_Point *where = &(ev->canvas);
    Evas_Modifier *modifiers = ev->modifiers;
    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Control = evas_key_modifier_is_set(modifiers, "Control");

    switch (ev->button) {
    case 1: do_mouse(where->x, where->y, LEFT_BUTTON, MOUSE_DOWN); break;
    case 3: do_mouse(where->x, where->y, RIGHT_BUTTON, MOUSE_DOWN); break;
    }
}

void
mouseUp(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Coord_Point *where = &(ev->canvas);
    Evas_Modifier *modifiers = ev->modifiers;
    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Control = evas_key_modifier_is_set(modifiers, "Control");

    /* Control left and right click: position bar lines */
    if (Control) {
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
    /* Remember where they clicked down */
    static int mouse_down_x, mouse_down_y;

    /* What modifier keys were held when the mouse was clicked down? */
    static int mouse_down_shift = 0;;
    static int mouse_down_ctrl = 0;;

    double when = (screen_x - disp_offset) * step;

    if (down) {
	mouse_down_x = screen_x;
	mouse_down_y = screen_y;
	mouse_down_shift = Shift;
	mouse_down_ctrl = Control;
    }

    if (!down && Control) switch (button) {
    case LEFT_BUTTON:	set_bar_left_time(when);	break;
    case RIGHT_BUTTON:	set_bar_right_time(when);	break;
    }
}
