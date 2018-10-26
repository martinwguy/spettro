/* lock.h: Declarations for lock.c */

extern void lock_fftw3(void);
extern void unlock_fftw3(void);

extern bool lock_audio_file(void);
extern bool unlock_audio_file(void);

extern bool lock_list(void);
extern bool unlock_list(void);

extern bool lock_window(void);
extern bool unlock_window(void);
