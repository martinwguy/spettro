/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* From sndfile-tools/src/spectrum.h */

#ifndef SPECTRUM_H

#include <fftw3.h>	/* for fftw_plan */

typedef struct
{	int speclen;
	window_function_t wfunc;
	fftwf_plan plan;

	float *time_domain;
	float *window;
	float *freq_domain;
	float *mag_spec;
} spectrum;

extern spectrum *create_spectrum(int speclen, window_function_t window_function);
extern void destroy_spectrum(spectrum *spec);
extern void calc_magnitude_spectrum(spectrum *spec);

#define SPECTRUM_H
#endif
