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
 * config.h - Select toolkit compenents to use.
 */

#ifndef CONFIG_H

/* Compile-time toolkit selectors */
#if USE_EMOTION
# define EMOTION_AUDIO	1
# define EVAS_VIDEO	1
# define ECORE_TIMER	1
# define ECORE_MAIN	1
# define ECORE_LOCKS	1
#elif USE_SDL
# define SDL_AUDIO	1
# define SDL_VIDEO	1
# define SDL_TIMER	1
# define SDL_MAIN	1
# define SDL_LOCKS	1
#elif USE_EMOTION_SDL
# define SDL_AUDIO	1
# define EVAS_VIDEO	1
# define ECORE_TIMER	1
# define ECORE_MAIN	1
# define ECORE_LOCKS	1
#else
# error "Define one of USE_EMOTION USE_SDL and USE_EMOTION_SDL in Makefile.am"
#endif

#if USE_SDL || USE_EMOTION_SDL
# include <SDL.h>
# if SDL_MAJOR_VERSION == 1
#  define SDL1 1
#  define SDL_PUSHEVENT_SUCCESS 0
# else
#  define SDL2 1
#  define SDL_PUSHEVENT_SUCCESS 1
# endif
#endif

#define CONFIG_H
#endif
