/*
 * Code to provide mutexes to protect non-thread-safe libraries and functions
 */

#include <Ecore.h>
#include "spettro.h"
#include "lock.h"

static Eina_Lock fftw3_mutex;
static bool fftw3_initialized = FALSE;

bool
lock_fftw3()
{
    if (!fftw3_initialized) {
	if (eina_lock_new(&fftw3_mutex) != EINA_TRUE)
	    return(FALSE);
	fftw3_initialized = TRUE;
    }

    if (eina_lock_take(&fftw3_mutex) != EINA_LOCK_SUCCEED)
	return(FALSE);

    return(TRUE);
}

bool
unlock_fftw3()
{
    if (eina_lock_release(&fftw3_mutex) != EINA_LOCK_SUCCEED) {
	return(FALSE);
    }
    return(TRUE);
}

static Eina_Lock audiofile_mutex;
static bool audiofile_initialized = FALSE;

bool
lock_audiofile()
{
    if (!audiofile_initialized) {
	if (eina_lock_new(&audiofile_mutex) != EINA_TRUE)
	    return(FALSE);
	audiofile_initialized = TRUE;
    }

    if (eina_lock_take(&audiofile_mutex) != EINA_LOCK_SUCCEED)
	return(FALSE);

    return(TRUE);
}

bool
unlock_audiofile()
{
    if (eina_lock_release(&audiofile_mutex) != EINA_LOCK_SUCCEED) {
	return(FALSE);
    }
    return(TRUE);
}
