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

extern void do_mouse_move(unsigned screen_x, unsigned screen_y);

#if EVAS_VIDEO
#include <Evas.h>
extern void mouseDown(void *data, Evas *e, Evas_Object *obj, void *event_info);
extern void mouseUp(void *data, Evas *e, Evas_Object *obj, void *event_info);
extern void mouseMove(void *data, Evas *e, Evas_Object *obj, void *event_info);
#endif
