/* speclen.h: Declarations for speclen.c */

extern int fftfreq_to_speclen(double fftfreq, double sr);
extern int speclen;	/* Spectral data length (=fftsize/2) */
