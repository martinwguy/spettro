/*
 * audio_file.h - include file for clients of audio_file.c
 */

#ifndef AUDIO_FILE_H

#if USE_LIBAUDIOFILE

#include <audiofile.h>

typedef struct audio_file {
	AFfilehandle af;
	unsigned long samplerate;
	unsigned long frames;	/* The file has (frames*channels) samples */
	unsigned channels;
} audio_file_t;

#elif USE_LIBSNDFILE

#include <sndfile.h>

typedef struct audio_file {
	SNDFILE *sndfile;
	unsigned long samplerate;
	unsigned long frames;
	unsigned channels;
} audio_file_t;

#else
#error "Define USE_LIBAUDIOFILE or USE_LIBSNDFILE"
#endif

typedef enum {
	af_double,
	af_signed,	/* 16-bit native endian */
} af_format;

/* Return a handle for the audio file, NULL on failure */
extern audio_file_t *open_audio_file(char *filename);

extern int read_audio_file(audio_file_t *audio_file, char *data,
			   af_format format, int channels,
			   int start,	/* In frames offset from 0.0 */
			   int nframes);

extern void close_audio_file(audio_file_t *audio_file);

#define audio_file_length_in_frames(audio_file) ((int)(audio_file)->frames)
#define audio_file_channels(audio_file) ((int)(audio_file)->channels)

/* Audio file info */
extern double		audio_length;	/* Length of the audio in seconds */
extern double		sample_rate;	/* SR of the audio in Hertz */

# define AUDIO_FILE_H
#endif /* HAVE_INCLUDED_AUDIO_FILE_H */
