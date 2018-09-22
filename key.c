/*
 * key.c: Code to handle keypress events, mapping them from the toolkit's
 * encoding to our own key names.
 */

#include "spettro.h"
#include "key.h"

bool Shift, Control;

#if ECORE_MAIN
/*
 * Keypress events
 *
 * Other interesting key names are:
 *	"Prior"		PgUp
 *	"Next"		PgDn
 *	"XF86AudioPlay"	Media button >
 *	"XF86AudioStop"	Media button []
 *	"XF86AudioPrev"	Media button <<
 *	"XF86AudioNext"	Media button >>
 *
 * The SDL equivalent of this is in SDL's main lood at the end of main().
 */

void
keyDown(void *data, Evas *evas, Evas_Object *obj, void *einfo)
{
    Evas_Event_Key_Down *ev = einfo;
    const Evas_Modifier *mods = evas_key_modifier_get(evas);
    enum key key = KEY_NONE;

    Shift = evas_key_modifier_is_set(mods, "Shift");
    Control = evas_key_modifier_is_set(mods, "Control");

    /* Handle single-character strings separately, not for speed but to avoid
     * tons of compiler warnings "array index 2 is past the end of the array"
     * due to glibc's stupid 17-line #define for strcmp().
     */
    if (ev->key[1] == '\0') switch (ev->key[0]) {
	case 'q': key = KEY_QUIT;		break;
	case 'c': if (Control) key = KEY_QUIT;	break;
	case 'x': case 'X': key = KEY_X;	break;
	case 'y': case 'Y': key = KEY_Y;	break;
	case 'p': key = KEY_P;			break;
	case 's': key = KEY_S;			break;
	case 'g': key = KEY_G;			break;
	case 't': key = KEY_T;			break;
	case 'f': key = KEY_F;			break;
	case 'l': key = KEY_BAR_START;		break;
	case 'r': if (Control) { key = KEY_REDRAW; break; }
		  key = KEY_BAR_END;		break;
    }
    else if (!strcmp(ev->key, "space"))
	key = KEY_SPACE;
    else if (!strcmp(ev->key, "Left") || !strcmp(ev->key, "KP_Left"))
	key = KEY_LEFT;
    else if (!strcmp(ev->key, "Right") || !strcmp(ev->key, "KP_Right"))
	key = KEY_RIGHT;
    else if (!strcmp(ev->key, "Up") || !strcmp(ev->key, "KP_Up"))
	key = KEY_UP;
    else if (!strcmp(ev->key, "Down") || !strcmp(ev->key, "KP_Down"))
	key = KEY_DOWN;
    else if (!strcmp(ev->key, "plus") || !strcmp(ev->key, "KP_Add"))
	key = KEY_PLUS;
    else if (!strcmp(ev->key, "minus") || !strcmp(ev->key, "KP_Subtract"))
	key = KEY_MINUS;
    else if (!strcmp(ev->key, "asterisk") || !strcmp(ev->key, "KP_Multiply"))
	key = KEY_STAR;
    else if (!strcmp(ev->key, "slash") || !strcmp(ev->key, "KP_Divide"))
	key = KEY_SLASH;
    else if (!strcmp(ev->key, "bracketleft"))
	key = KEY_BAR_START;
    else if (!strcmp(ev->key, "bracketright"))
	key = KEY_BAR_END;
/*
    else
	fprintf(stderr, "Key \"%s\" was pressed.\n", ev->key);
 */

    do_key(key);
}
#elif SDL_MAIN

enum key
sdl_key_decode(SDL_Event *eventp)
{
    enum key key     = KEY_NONE;
    switch (eventp->key.keysym.sym) {
    case SDLK_q:	     key = KEY_QUIT;	break;
    case SDLK_c:if (Control) key = KEY_QUIT;	break;
    case SDLK_SPACE:	     key = KEY_SPACE;	break;
    case SDLK_LEFT:	     key = KEY_LEFT;	break;
    case SDLK_RIGHT:	     key = KEY_RIGHT;	break;
    case SDLK_UP:	     key = KEY_UP;	break;
    case SDLK_DOWN:	     key = KEY_DOWN;	break;
    case SDLK_x:	     key = KEY_X;	break;
    case SDLK_y:	     key = KEY_Y;	break;
    case SDLK_PLUS:	     key = KEY_PLUS;	break;
    case SDLK_MINUS:	     key = KEY_MINUS;	break;
    case SDLK_ASTERISK:	     key = KEY_STAR;	break;
    case SDLK_SLASH:	     key = KEY_SLASH;	break;
    case SDLK_p:	     key = KEY_P;	break;
    case SDLK_s:	     key = KEY_S;	break;
    case SDLK_g:	     key = KEY_G;	break;
    case SDLK_t:	     key = KEY_T;	break;
    case SDLK_f:	     key = KEY_F;	break;
    case SDLK_l:
    case SDLK_LEFTBRACKET:   key = KEY_BAR_START;break;
    case SDLK_r:
	if (Control)	   { key = KEY_REDRAW; break; }
    case SDLK_RIGHTBRACKET:  key = KEY_BAR_END; break;
    default: break;
    }
    return key;
}
#endif
