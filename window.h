/* window.h: Declarationg for window.c */

#ifndef WINDOW_H

typedef enum {
    ANY = -1,
    RECTANGULAR,
    KAISER,
    NUTTALL,
    HANN,
    HAMMING,
    BARTLETT,
    BLACKMAN,
    DOLPH,
    NUMBER_OF_WINDOW_FUNCTIONS
} window_function_t;

extern double *get_window(window_function_t wfunc, int datalen);
extern void free_windows(void);
extern const char *window_name(window_function_t wfunc);
extern const char window_key(window_function_t wfunc);

#define WINDOW_H
#endif
