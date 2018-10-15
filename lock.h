/* lock.h: Declarations for lock.c */

extern void lock_fftw3(void);
extern void unlock_fftw3(void);

extern bool lock_audiofile(void);
extern bool unlock_audiofile(void);

extern bool lock_list(void);
extern bool unlock_list(void);

extern bool lock_window(void);
extern bool unlock_window(void);
