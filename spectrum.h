/* From sndfile-tools/src/spectrum.h */

typedef struct
{	int speclen;
	enum WINDOW_FUNCTION wfunc;
	fftw_plan plan;

	double *time_domain;
	double *window;
	double *freq_domain;
	float *mag_spec;

	/* double data []; */
} spectrum;

spectrum *create_spectrum(int speclen, enum WINDOW_FUNCTION window_function);

void destroy_spectrum(spectrum *spec);

double calc_magnitude_spectrum(spectrum *spec);
