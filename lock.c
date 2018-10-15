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
static lock_t audiofile_lock;
static bool audiofile_lock_is_initialized = FALSE;
static lock_t list_lock;
static bool list_lock_is_initialized = FALSE;
static lock_t window_lock;
static bool window_lock_is_initialized = FALSE;

bool
lock_fftw3()
{
    if (!initialize(&fftw3_lock, &fftw3_lock_is_initialized))
	return FALSE;
    else
	return do_lock(&fftw3_lock);
}

bool
unlock_fftw3()
{
    return do_unlock(&fftw3_lock);
}

bool
lock_audiofile()
{
    if (!initialize(&audiofile_lock, &audiofile_lock_is_initialized))
	return FALSE;
    else
	return do_lock(&audiofile_lock);
}

bool
unlock_audiofile()
{
    return do_unlock(&audiofile_lock);
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
