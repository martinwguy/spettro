/*
 * libsndfile.h - include file for clients of libsndfile.c
 * implemented using the same function interface as audiofile.c
 * but using libsndfile instead of libaudiofile.
 */

#include <audiofile.h>

typedef struct audio_file {
	SNDFILE *sndfile;
	unsigned long samplerate;
	unsigned long frames;
} audio_file_t;

/* Returns a handle for the audio file, NULL on failure */
extern audio_file_t *open_audio_file(char *filename);

extern int audio_file_length_in_frames(audio_file_t *audio_file);

extern double audio_file_sampling_rate(audio_file_t *audio_file);

extern int read_mono_audio_double(audio_file_t *audio_file, double *data,
				  int start,	/* In frames offset from 0.0 */
				  int nframes);

extern void close_audio_file(audio_file_t *audio_file);
