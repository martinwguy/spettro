/*
 * mouse.h - Declarations for mouse.c
 */

/* Our names for the buttons and mouse-down/mouse-up events */
#define LEFT_BUTTON 1
#define RIGHT_BUTTON 2
#define MOUSE_DOWN 1
#define MOUSE_UP 0

extern void do_mouse(unsigned screen_x, unsigned screen_y, int button, bool down);

#if EVAS_VIDEO
#include <Evas.h>
extern void mouseDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
#endif
