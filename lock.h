/* lock.h: Declarations for lock.c */

bool lock_fftw3(void);
bool unlock_fftw3(void);

bool lock_audiofile(void);
bool unlock_audiofile(void);

bool lock_list(void);
bool unlock_list(void);

bool lock_window(void);
bool unlock_window(void);
