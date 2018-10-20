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
 *	"XF86AudioPlay"	Media button >
 *	"XF86AudioStop"	Media button []
 *	"XF86AudioPrev"	Media button <<
 *	"XF86AudioNext"	Media button >>
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
	case 'k': key = KEY_K;			break;
	case 's': key = KEY_S;			break;
	case 'g': key = KEY_G;			break;
	case 'p': key = KEY_P;			break;
	case 't': key = KEY_T;			break;
	case 'f': key = KEY_F;			break;
	case 'l': key = KEY_L;			break;
	case 'r': key = Control ? KEY_REDRAW : KEY_R; break;
	case 'b': key = KEY_STAR;		break;
	case 'd': key = KEY_SLASH;		break;
	/* Other window function keys */
	case 'h': key = KEY_H;			break;
	case 'n': key = KEY_N;			break;
    }
    else if (!strcmp(ev->key, "space"))
	key = KEY_SPACE;
    else if (!strcmp(ev->key, "Left") || !strcmp(ev->key, "KP_Left"))
	key = KEY_LEFT;
    else if (!strcmp(ev->key, "Right") || !strcmp(ev->key, "KP_Right"))
	key = KEY_RIGHT;
    else if (!strcmp(ev->key, "Home") || !strcmp(ev->key, "KP_Home"))
	key = KEY_HOME;
    else if (!strcmp(ev->key, "End") || !strcmp(ev->key, "KP_End"))
	key = KEY_END;
    else if (!strcmp(ev->key, "Up") || !strcmp(ev->key, "KP_Up"))
	key = KEY_UP;
    else if (!strcmp(ev->key, "Down") || !strcmp(ev->key, "KP_Down"))
	key = KEY_DOWN;
    else if (!strcmp(ev->key, "Prior"))
	key = KEY_PGUP;
    else if (!strcmp(ev->key, "Next"))
	key = KEY_PGDN;
    else if (!strcmp(ev->key, "plus") || !strcmp(ev->key, "KP_Add"))
	key = KEY_PLUS;
    else if (!strcmp(ev->key, "minus") || !strcmp(ev->key, "KP_Subtract"))
	key = KEY_MINUS;
    else if (!strcmp(ev->key, "asterisk") || !strcmp(ev->key, "KP_Multiply"))
	key = KEY_STAR;
    else if (!strcmp(ev->key, "slash") || !strcmp(ev->key, "KP_Divide"))
	key = KEY_SLASH;
    else
	fprintf(stderr, "Key \"%s\" doesn't do anything.\n", ev->key);

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
    case SDLK_HOME:	     key = KEY_HOME;	break;
    case SDLK_END:	     key = KEY_END;	break;
    case SDLK_UP:	     key = KEY_UP;	break;
    case SDLK_DOWN:	     key = KEY_DOWN;	break;
    case SDLK_PAGEUP:	     key = KEY_PGUP;	break;
    case SDLK_PAGEDOWN:	     key = KEY_PGDN;	break;
    case SDLK_x:	     key = KEY_X;	break;
    case SDLK_y:	     key = KEY_Y;	break;
    case SDLK_PLUS:	     key = KEY_PLUS;	break;
    case SDLK_MINUS:	     key = KEY_MINUS;	break;
    case SDLK_ASTERISK:	     key = KEY_STAR;	break;
    case SDLK_SLASH:	     key = KEY_SLASH;	break;
    case SDLK_k:	     key = KEY_K;	break;
    case SDLK_s:	     key = KEY_S;	break;
    case SDLK_g:	     key = KEY_G;	break;
    case SDLK_p:	     key = KEY_P;	break;
    case SDLK_t:	     key = KEY_T;	break;
    case SDLK_f:	     key = KEY_F;	break;
    case SDLK_l:	     key = KEY_L;	break;
    case SDLK_r: key = Control ? KEY_REDRAW : KEY_R; break;
    case SDLK_b:	     key = KEY_STAR;	break;
    case SDLK_d:	     key = KEY_SLASH;	break;
	/* Unclaimed window function keys */
    case SDLK_h:	     key = KEY_H;	break;
    case SDLK_n:	     key = KEY_N;	break;
    default: break;
    }
    return key;
}
#endif
