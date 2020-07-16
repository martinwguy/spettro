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

/*
 * audio_file.h - include file for clients of audio_file.c
 */

#ifndef AUDIO_FILE_H

#if USE_LIBAUDIOFILE
# include <audiofile.h>
#elif USE_LIBSNDFILE
# include <sndfile.h>
#elif USE_LIBSOX
# include <sox.h>
#else
# error "Define one of USE_LIBAUDIOFILE USE_LIBSNDFILE USE_LIBSOX"
#endif

#if USE_LIBMPG123
#include <mpg123.h>
#endif

typedef struct audio_file {
	char *filename;
#if USE_LIBAUDIOFILE
	AFfilehandle afh;
#elif USE_LIBSNDFILE
 	SNDFILE *sndfile;
#elif USE_LIBSOX
	sox_format_t *sf;
#endif
#if USE_LIBMPG123
	mpg123_handle *mh;
	FILE *in;
	int framesize;
#endif
	double sample_rate;
	unsigned long frames;	/* The file has (frames*channels) samples */
	unsigned channels;
	short *audio_buf;	/* The required audio data as 16-bit signed,
    				 * with same number of channels as audio file */
	int audio_buflen;	/* Memory allocated to audio_buf[] in samples */
} audio_file_t;

typedef enum {
	af_float,   /* mono floats */
	af_signed,  /* 16-bit native endian, same number of channels as the input file */
} af_format_t;

extern audio_file_t *current_audio_file(void);

/* Return a handle for the audio file, NULL on failure */
extern audio_file_t *open_audio_file(char *filename);

extern int read_audio_file(char *data,
			   af_format_t format, int channels,
			   int start,	/* In frames offset from 0.0 */
			   int nframes);

extern void close_audio_file(audio_file_t *audio_file);

/* Utility functions */

/* The length of an audio file in seconds */
extern double audio_file_length(void);

/* What's the sample rate of the audio file at the current playing position? */
extern double current_sample_rate(void);

# define AUDIO_FILE_H
#endif
