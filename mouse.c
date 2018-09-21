/*
 * mouse.c - Mouse-handling code
 */

#include "spettro.h"

#include "mouse.h"

#include "key.h"	/* for Shift and Control */
#include "overlay.h"	/* for set_bar_*_time() */
#include "main.h"	/* for various GUI variables */

#if ECORE_MAIN
void
mouseDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Mouse_Down *ev = einfo;
    Evas_Coord_Point *where = &(ev->canvas);
    Evas_Modifier *modifiers = ev->modifiers;
    Shift = evas_key_modifier_is_set(modifiers, "Shift");
    Control = evas_key_modifier_is_set(modifiers, "Control");

    /* Bare left and right click: position bar lines */
    switch (ev->button) {
    case 1: do_mouse(where->x, where->y, LEFT_BUTTON, MOUSE_DOWN); break;
    case 3: do_mouse(where->x, where->y, RIGHT_BUTTON, MOUSE_DOWN); break;
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
