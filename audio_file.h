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
	/* keeps its private data as statics in filtering_audio.c */
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

/* Audio file info for everybody */
extern double		audio_length;	/* Length of the audio in seconds */
extern double		sample_rate;	/* SR of the audio in Hertz */

# define AUDIO_FILE_H
#endif /* HAVE_INCLUDED_AUDIO_FILE_H */
