/* key.h: Declarations for key.c */

#ifndef KEY_H

#if ECORE_MAIN
#include <Evas.h>
extern void keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo);
#elif SDL_MAIN
#include <SDL.h>
extern enum key sdl_key_decode(SDL_Event *eventp);
#endif

/* Driver-independent keypress names and modifiers */
enum key {
    KEY_NONE,
    KEY_QUIT,
    KEY_SPACE,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_UP,
    KEY_DOWN,
    KEY_X,
    KEY_Y,
    KEY_PLUS,
    KEY_MINUS,
    KEY_STAR,
    KEY_SLASH,
    KEY_K,
    KEY_S,
    KEY_G,
    KEY_T,
    KEY_F,
    KEY_REDRAW,
    KEY_BAR_START,
    KEY_BAR_END,
};
extern bool Shift, Control;

/* Functions supplied by key.c */
#if ECORE_MAIN
extern void keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo);
#elif SDL_MAIN
enum key sdl_key_decode(SDL_Event *eventp);
#endif

/* Callback supplied by the caller */
extern void do_key(enum key);

#define KEY_H
#endif
