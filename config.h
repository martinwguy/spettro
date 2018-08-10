/*
 * config.h - Select toolkit compenents to use.
 */

/* Compile-time toolkit selectors */
#if USE_EMOTION
# define EMOTION_AUDIO	1
# define EVAS_VIDEO	1
# define ECORE_TIMER	1
# define ECORE_MAIN	1
#elif USE_SDL
# define SDL_AUDIO	1
# define SDL_VIDEO	1
# define SDL_TIMER	1
# define SDL_MAIN	1
#elif USE_EMOTION_SDL
# define SDL_AUDIO	1
# define EVAS_VIDEO	1
# define ECORE_TIMER	1
# define ECORE_MAIN	1
#endif
