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
# error "Define USE_LIBAUDIOFILE or USE_LIBSNDFILE or USE_LIBSOX"
#endif

typedef struct audio_file {
	char *filename;
#if USE_LIBAUDIOFILE
	AFfilehandle af;
#elif USE_LIBSNDFILE
 	SNDFILE *sndfile;
#elif USE_LIBSOX
	sox_format_t *sf;
#endif
	unsigned long samplerate;
	unsigned long frames;	/* The file has (frames*channels) samples */
	unsigned channels;
} audio_file_t;

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

/* Audio file info for everybody */
extern double		audio_length;	/* Length of the audio in seconds */
extern double		sample_rate;	/* SR of the audio in Hertz */

# define AUDIO_FILE_H
#endif /* HAVE_INCLUDED_AUDIO_FILE_H */
