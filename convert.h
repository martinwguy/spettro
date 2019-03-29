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

/* convert.h: Declarations for convert.c */

extern double v_pixel_freq_ratio(void);
extern double magindex_to_frequency(int k);
extern double frequency_to_specindex(double freq, double sample_rate, int speclen);
extern int fft_freq_to_speclen(double fft_freq, double sample_rate);

#define NOTE_A440	48  /* A above middle C */
#define A0_FREQUENCY	27.5

extern double note_name_to_freq(const char *note);
extern double note_number_to_freq(const int note);
extern int freq_to_magindex(double freq);
