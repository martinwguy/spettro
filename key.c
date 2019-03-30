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
 * key.c: Code to handle keypress events, mapping them from the toolkit's
 * encoding to our own key names.
 */

#include "spettro.h"
#include "key.h"

#include "ctype.h"	/* for tolower() */

static void map_to_lower_case(char *s);

bool Shift, Control;

/*
 * Keypress events
 *
 * Ecore works OK with all keys.
 * SDL1 (KEYDOWN event) doesn't apply Shift or AltGr to key presses,
 *	so you can't get keys only available with Shift or AltGr
 *	(e.g. If the keyboard's * is shift-+, you can't use it for brightness)
 *	Apparently this is because that's more suitable for games.
 *	The CACA port over ssh returns "unknown key" for capital letters.
 * SDL2's KEYDOWN events are the same, but it also has a TEXTINPUT event
 *	which does respond to Shift and AltGr, but ignores
 *	Home, End, PgUp, PgDown and the keypad arrow keys.
 *	We enable both types of events for SDL2 and jockey between them
 *	hoping to get all keypresses but no duplicates!
 *	The TEXTINPUT event ignores the keypad, the arrow keys and
 *	Control-X combinations.
 *
 * Other Ecore key names seen by Emotion:
 *	"XF86HomePage"	|^|
 *	"XF86Mail"	|v|
 *
 * SDL1 returns "unknown key" for |>/|| [] |<< and >>|, HomePage and Mail,
 * SDL2 doesn't even get a keypress event for these.
 */

#if ECORE_MAIN
void
keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
#elif SDL_MAIN
void
sdl_keydown(SDL_Event *eventp)
#endif
{
#if ECORE_MAIN
    Evas_Event_Key_Down *ev = einfo;
    const Evas_Modifier *mods = evas_key_modifier_get(evas);
#elif SDL_MAIN && SDL2
    bool numlock;
#endif

    enum key key = KEY_NONE;
    char *name;

#if ECORE_MAIN
    name = strdup(ev->key);	/* Copy because we'll lower-case it */
#elif SDL_MAIN
# if SDL1
    name = strdup(SDL_GetKeyName(eventp->key.keysym.sym));
# elif SDL2
    switch (eventp->type) {
    case SDL_TEXTINPUT:
    	name = strdup(eventp->text.text);
	break;
    case SDL_KEYDOWN:
        name = strdup(SDL_GetKeyName(eventp->key.keysym.sym));
    	break;
    default:
        abort();
    }
# endif
#endif

#if ECORE_MAIN
    Shift = evas_key_modifier_is_set(mods, "Shift");
    Control = evas_key_modifier_is_set(mods, "Control");
#elif SDL_MAIN
    Shift = !!(SDL_GetModState() & KMOD_SHIFT);
    Control = !!(SDL_GetModState() & KMOD_CTRL);
# if SDL2
    numlock = !!(SDL_GetModState() & KMOD_NUM);
# endif
#endif

    map_to_lower_case(name);

    /* Handle single-character strings separately, not for speed but to avoid
     * tons of compiler warnings "array index 2 is past the end of the array"
     * due to glibc's stupid 17-line #define for strcmp().
     */
#if SDL_MAIN && SDL2
    /* Single-character presses are handled best by TEXTINPUT,
     * but named characters and control characters only come by KEYDOWN
     */
    if (name[1] == '\0' && (eventp->type == SDL_KEYDOWN && !Control)) return;
    if (eventp->type == SDL_TEXTINPUT || Control)
#endif
    if (name[1] == '\0') switch (name[0]) {
	case 'q': key = KEY_Q;			break;
	case 'c': key = KEY_C;			break;
	case 'x': key = KEY_X;			break;
	case 'y': key = KEY_Y;			break;
	case 'k': key = KEY_K;			break;
	case 's': key = KEY_S;			break;
	case 'g': key = KEY_G;			break;
	case 'o': key = KEY_O;			break;
	case 'p': key = KEY_P;			break;
	case 't': key = KEY_T;			break;
	case 'f': key = KEY_F;			break;
	case 'l': key = KEY_L;			break;
	case 'r': key = KEY_R;			break;
	case 'b': key = KEY_B;			break;
	case 'd': key = KEY_D;			break;
        case 'a': key = KEY_A;			break;
        case 'w': key = KEY_W;			break;
	/* Other window function keys */
	case 'h': key = KEY_H;			break;
	case 'n': key = KEY_N;			break;
	case 'm': key = KEY_M;			break;
	/* Avanti! */
	case '0': key = KEY_0;			break;
	case '1': key = KEY_1;			break;
	case '2': key = KEY_2;			break;
	case '3': key = KEY_3;			break;
	case '4': key = KEY_4;			break;
	case '5': key = KEY_5;			break;
	case '6': key = KEY_6;			break;
	case '7': key = KEY_7;			break;
	case '8': key = KEY_8;			break;
	case '9': key = KEY_9;			break;
        case '+': key = KEY_PLUS;		break;
        case '-': key = KEY_MINUS;		break;
        case '*': key = KEY_STAR;		break;
        case '/': key = KEY_SLASH;		break;
	/* SDL2 TEXTINPUT sends " " for space, but KEYDOWN send "space" too
	 * so we use the latter rather than get two. */
	case ' ': break;
	default:
	    goto moan;
    }
    if (key != KEY_NONE) goto done;

#if SDL_MAIN && SDL2
    if (eventp->type == SDL_KEYDOWN) {
#endif
    /* names common to several toolkits */
         if (!strcmp(name, "escape"))			key = KEY_ESC;
    else if (!strcmp(name, "space"))			key = KEY_SPACE;
    else if (!strcmp(name, "left"))			key = KEY_LEFT;
    else if (!strcmp(name, "right"))			key = KEY_RIGHT;
    else if (!strcmp(name, "home"))			key = KEY_HOME;
    else if (!strcmp(name, "end"))			key = KEY_END;
    else if (!strcmp(name, "up"))			key = KEY_UP;
    else if (!strcmp(name, "down"))			key = KEY_DOWN;
    else if (!strcmp(name, "f1"))			key = KEY_F1;
    else if (!strcmp(name, "f2"))			key = KEY_F2;
    else if (!strcmp(name, "f3"))			key = KEY_F3;
    else if (!strcmp(name, "f4"))			key = KEY_F4;
    else if (!strcmp(name, "f5"))			key = KEY_F5;
    else if (!strcmp(name, "f6"))			key = KEY_F6;
    else if (!strcmp(name, "f7"))			key = KEY_F7;
    else if (!strcmp(name, "f8"))			key = KEY_F8;
    else if (!strcmp(name, "f9"))			key = KEY_F9;
    else if (!strcmp(name, "f10"))			key = KEY_F10;
    else if (!strcmp(name, "f11"))			key = KEY_F11;
    else if (!strcmp(name, "f12"))			key = KEY_F12;
#if ECORE_MAIN
    else if (!strcmp(name, "prior"))			key = KEY_PGUP;
    else if (!strcmp(name, "next"))			key = KEY_PGDN;
    else if (!strcmp(name, "kp_end"))			key = KEY_END;
    else if (!strcmp(name, "kp_down"))			key = KEY_DOWN;
    else if (!strcmp(name, "kp_next"))			key = KEY_PGDN;
    else if (!strcmp(name, "kp_left"))			key = KEY_LEFT;
    else if (!strcmp(name, "kp_right"))			key = KEY_RIGHT;
    else if (!strcmp(name, "kp_home"))			key = KEY_HOME;
    else if (!strcmp(name, "kp_up"))			key = KEY_UP;
    else if (!strcmp(name, "kp_prior"))			key = KEY_PGUP;
    else if (!strcmp(name, "kp_add"))			key = KEY_PLUS;
    else if (!strcmp(name, "kp_subtract"))		key = KEY_MINUS;
    else if (!strcmp(name, "kp_multiply"))		key = KEY_STAR;
    else if (!strcmp(name, "kp_divide"))		key = KEY_SLASH;
    else if (!strcmp(name, "asterisk"))			key = KEY_STAR;
    else if (!strcmp(name, "slash"))			key = KEY_SLASH;
    else if (!strcmp(name, "xf86audioplay"))		key = KEY_PLAY;
    else if (!strcmp(name, "xf86audiostop"))		key = KEY_STOP;
    else if (!strcmp(name, "xf86audioprev"))		key = KEY_PREV;
    else if (!strcmp(name, "xf86audionext"))		key = KEY_NEXT;
    else if (!strcmp(name, "xf86audioraisevolume"))	key = KEY_0;
    else if (!strcmp(name, "xf86audiolowervolume"))	key = KEY_9;
    else if (!strcmp(name, "stop"))			key = KEY_STOP;
#elif SDL_MAIN
# if SDL1
    else if (!strcmp(name, "page up"))			key = KEY_PGUP;
    else if (!strcmp(name, "page down"))		key = KEY_PGDN;
    else if (!strcmp(name, "[1]"))			key = KEY_END;
    else if (!strcmp(name, "[2]"))			key = KEY_DOWN;
    else if (!strcmp(name, "[3]"))			key = KEY_PGDN;
    else if (!strcmp(name, "[7]"))			key = KEY_HOME;
    else if (!strcmp(name, "[6]"))			key = KEY_RIGHT;
    else if (!strcmp(name, "[7]"))			key = KEY_HOME;
    else if (!strcmp(name, "[8]"))			key = KEY_UP;
    else if (!strcmp(name, "[9]"))			key = KEY_PGUP;
    else if (!strcmp(name, "[+]"))			key = KEY_PLUS;
    else if (!strcmp(name, "[-]"))			key = KEY_MINUS;
    else if (!strcmp(name, "[*]"))			key = KEY_STAR;
    else if (!strcmp(name, "[/]"))			key = KEY_SLASH;
# elif SDL2
    else if (!strcmp(name, "pageup"))			key = KEY_PGUP;
    else if (!strcmp(name, "pagedown"))			key = KEY_PGDN;
    /* If numlock is on, we get both a TEXTINPUT and a KEYDOWN */
    else if (!strcmp(name, "keypad 1") && !numlock)	key = KEY_END;
    else if (!strcmp(name, "keypad 2") && !numlock)	key = KEY_DOWN;
    else if (!strcmp(name, "keypad 3") && !numlock)	key = KEY_PGDN;
    else if (!strcmp(name, "keypad 4") && !numlock)	key = KEY_LEFT;
    else if (!strcmp(name, "keypad 6") && !numlock)	key = KEY_RIGHT;
    else if (!strcmp(name, "keypad 7") && !numlock)	key = KEY_HOME;
    else if (!strcmp(name, "keypad 8") && !numlock)	key = KEY_UP;
    else if (!strcmp(name, "keypad 9") && !numlock)	key = KEY_PGUP;
    /* Ignore these as they also come in as a TEXTINPUT character "+" etc */
    else if (!strcmp(name, "keypad +"))			key = KEY_NONE;
    else if (!strcmp(name, "keypad -"))			key = KEY_NONE;
    else if (!strcmp(name, "keypad *"))			key = KEY_NONE;
    else if (!strcmp(name, "keypad /"))			key = KEY_NONE;
# endif
#endif
    /* Don't report shift/ctrl presses. "left shift" is SDL[12] */
#if ECORE_MAIN
    else if (!strcmp(name, "shift_l"))			key = KEY_NONE;
    else if (!strcmp(name, "shift_r"))			key = KEY_NONE;
    else if (!strcmp(name, "control_l"))		key = KEY_NONE;
    else if (!strcmp(name, "control_r"))		key = KEY_NONE;
#elif SDL_MAIN
    else if (!strcmp(name, "left shift"))		key = KEY_NONE;
    else if (!strcmp(name, "right shift"))		key = KEY_NONE;
    else if (!strcmp(name, "left ctrl"))		key = KEY_NONE;
    else if (!strcmp(name, "right ctrl"))		key = KEY_NONE;
#endif
    else
moan:	fprintf(stderr, "Key \"%s\" doesn't do anything.\n", name);
#if SDL_MAIN && SDL2
    }
#endif

done:
    free(name);
    do_key(key);
}

static void
map_to_lower_case(char *s)
{
    while (*s != '\0') { *s = tolower(*s); s++; }
}
