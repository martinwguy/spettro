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

/* key.h: Declarations for key.c */


#ifndef INCLUDED_KEY_H

#if ECORE_MAIN
#include <Evas.h>
extern void keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo);
#elif SDL_MAIN
#include <SDL.h>
extern void sdl_keydown(SDL_Event *eventp);
#endif

/*
 * Driver-independent keypress names
 *
 * This table must have the same entries in the same order as
 * the key_fns[] table in do_key.c
 */
enum key {
    KEY_NONE,
    KEY_Q,
    KEY_C,
    KEY_ESC,
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
#if 0
    KEY_STAR,
    KEY_SLASH,
#endif
    KEY_K,
    KEY_S,
    KEY_G,
    KEY_O,
    KEY_P,
    KEY_T,
    KEY_F,
    KEY_L,
    KEY_R,
    KEY_B,
    KEY_D,
    KEY_A,
    KEY_W,
    KEY_M,
    KEY_H,
    KEY_N,
    KEY_0,
    KEY_9,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
#if ECORE_MAIN
    /* Extended keyboard's >/|| [] |<< and >>| buttons */
    KEY_PLAY,
    KEY_STOP,
    KEY_PREV,
    KEY_NEXT,
#endif
};

/* typedef key_t is already used by sys/types.h */
#define key_t enum key

extern bool Shift, Ctrl;

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
