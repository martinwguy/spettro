/*
 * interpolate.h - Header file for interpolate.c
 */

extern float interpolate(
	float *mag, int maglen, const float *spec, const int speclen,
	const double min_freq, const double max_freq, const double samplerate,
	const bool log_freq
	);
