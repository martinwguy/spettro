/* From sndfile-tools/src/spectrum.h */

#ifndef SPECTRUM_H

typedef struct
{	int speclen;
	window_function_t wfunc;
	fftw_plan plan;

	double *time_domain;
	double *window;
	double *freq_domain;
	float *mag_spec;

	/* double data []; */
} spectrum;

extern spectrum *create_spectrum(int speclen, window_function_t window_function);
extern void destroy_spectrum(spectrum *spec);
extern void calc_magnitude_spectrum(spectrum *spec);

#define SPECTRUM_H
#endif
