/*
 * main.h: Declarations for GUI state variables exported by main.c
 */
extern int disp_width;		/* Size of displayed drawing area in pixels */
extern int disp_height;
extern double disp_time; 	/* When in the audio file is the crosshair? */
extern int disp_offset;  	/* Crosshair is in which display column? */
extern double step;		/* time step per column = 1/ppsec */

/* The FFT-calculating threads' main loop */
#if ECORE_MAIN
extern void calc_heavy(void *data, Ecore_Thread *thread);
#elif SDL_MAIN
extern void *calc_heavy(void *data);
#endif

/* FFT calculating thread callbacks */
#if ECORE_MAIN
extern void calc_notify(void *data, Ecore_Thread *thread, void *msg_data);
#elif SDL_MAIN
extern void calc_notify(result_t *result);
#else
# error "Define ECORE_MAIN or SDL_MAIN"
#endif

/* The callback function used to report results from FFT calculations */
extern void calc_result(result_t *result);