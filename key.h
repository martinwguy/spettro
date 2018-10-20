/* key.h: Declarations for key.c */


#ifndef INCLUDED_KEY_H

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
    KEY_Q,
    KEY_C,
    KEY_SPACE,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_UP,
    KEY_DOWN,
    KEY_PGUP,
    KEY_PGDN,
    KEY_X,
    KEY_Y,
    KEY_PLUS,
    KEY_MINUS,
    KEY_STAR,
    KEY_SLASH,
    KEY_K,
    KEY_S,
    KEY_G,
    KEY_P,
    KEY_T,
    KEY_F,
    KEY_L,
    KEY_R,
    KEY_B,
    KEY_D,
    KEY_H,
    KEY_N,
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

#define INCLUDED_KEY_H
#endif
