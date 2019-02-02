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
#elif USE_LIBAV
  /* nothing */
#else
# error "Define one of USE_LIBAUDIOFILE USE_LIBSNDFILE USE_LIBSOX USE_LIBAV"
#endif

typedef struct audio_file {
	char *filename;
#if USE_LIBAUDIOFILE
	AFfilehandle af;
#elif USE_LIBSNDFILE
 	SNDFILE *sndfile;
#elif USE_LIBSOX
	sox_format_t *sf;
#elif USE_LIBAV
	/* keeps its private data as statics in libav.c */
#endif
	unsigned long sample_rate;
	unsigned long frames;	/* The file has (frames*channels) samples */
	unsigned channels;
} audio_file_t;

typedef enum {
	af_double,  /* mono doubles */
	af_signed,  /* 16-bit native endian, same channels as the input file */
} af_format_t;

/* Return a handle for the audio file, NULL on failure */
extern audio_file_t *open_audio_file(char *filename);

extern int read_audio_file(audio_file_t *audio_file, char *data,
			   af_format_t format, int channels,
			   int start,	/* In frames offset from 0.0 */
			   int nframes);

extern void close_audio_file(audio_file_t *audio_file);

/* Convenience function */
extern double audio_file_length(audio_file_t *audio_file);

/* Audio file info for everybody */
extern audio_file_t *	audio_file;

# define AUDIO_FILE_H
#endif /* HAVE_INCLUDED_AUDIO_FILE_H */
