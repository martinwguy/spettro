/*
 * interpolate.h - Header file for interpolate.c
 */

extern float interpolate(
	float *mag, int maglen, const float *spec, const int speclen,
	const double min_freq, const double max_freq, const double samplerate,
	int min_y, int max_y);

void free_interpolate_cache();
