/*
 * interpolate.h - Header file for interpolate.c
 */

extern float interpolate(float *logmag, const float *spec,
			 int from_y, int to_y);
void free_interpolate_cache();
