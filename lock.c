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
 * Code to provide mutexes to protect non-thread-safe libraries and functions
 */

#include "spettro.h"
#include "lock.h"

/*
 * Define the lock type and the locking and unlocking functions
 * according to the system we're using
 */

#if ECORE_LOCKS
# include <Ecore.h>
  typedef Eina_Lock lock_t;
# define do_lock(lockp)   (eina_lock_take(lockp) == EINA_LOCK_SUCCEED)
# define do_unlock(lockp) (eina_lock_release(lockp) == EINA_LOCK_SUCCEED)
#elif SDL_LOCKS
# include <SDL.h>
# include <SDL_thread.h>
  typedef SDL_mutex *lock_t;
# define do_lock(lockp)   (SDL_mutexP(*lockp) == 0)
# define do_unlock(lockp) (SDL_mutexV(*lockp) == 0)
#else
# error "Define one of ECORE_LOCKS and SDL_LOCKS"
#endif

/* The lock initialization function */

static bool
initialize(lock_t *lockp, bool *initp)
{
    if (!*initp) {
#if ECORE_LOCKS
	if (eina_lock_new(lockp) != EINA_TRUE)
#elif SDL_LOCKS
	if ( (*lockp = SDL_CreateMutex()) == NULL )
#endif
	    return(FALSE);
	*initp = TRUE;
    }
    return TRUE;
}

/*
 * Private data and public functions for the locks
 */

static lock_t fftw3_lock;
static bool fftw3_lock_is_initialized = FALSE;
static lock_t audio_file_lock;
static bool audio_file_lock_is_initialized = FALSE;
static lock_t list_lock;
static bool list_lock_is_initialized = FALSE;
static lock_t window_lock;
static bool window_lock_is_initialized = FALSE;

void
lock_fftw3()
{
    if (!initialize(&fftw3_lock, &fftw3_lock_is_initialized) ||
	!do_lock(&fftw3_lock)) {
	fprintf(stderr, "Cannot lock FFTW3\n");
	abort();
    }
}

void
unlock_fftw3()
{
    if (!do_unlock(&fftw3_lock)) {
	fprintf(stderr, "Cannot unlock FFTW3\n");
	abort();
    }
}

void
lock_audio_file()
{
    if (!initialize(&audio_file_lock, &audio_file_lock_is_initialized)) {
	fprintf(stderr, "Failed to initialize audio file lock.\n");
	exit(1);
    }
    if (!do_lock(&audio_file_lock)) {
	fprintf(stderr, "Failed to lock audio file.\n");
	exit(1);
    }
}

void
unlock_audio_file()
{
    if (!do_unlock(&audio_file_lock)) {
	fprintf(stderr, "Failed to unlock audio file.\n");
	exit(1);
    }
}

bool
lock_list()
{
    if (!initialize(&list_lock, &list_lock_is_initialized))
	return FALSE;
    else
	return do_lock(&list_lock);
}

bool
unlock_list()
{
    return do_unlock(&list_lock);
}

bool
lock_window()
{
    if (!initialize(&window_lock, &window_lock_is_initialized))
	return FALSE;
    else
	return do_lock(&window_lock);
}

bool
unlock_window()
{
    return do_unlock(&window_lock);
}
