/*
 * audiofile.h - include file for clients of audiofile.c
 *
 * Implemented using libaudiofile, which can only read a few formats.
 */

#include <audiofile.h>

typedef struct audio_file {
	AFfilehandle af;
} audio_file_t;

typedef enum {
	af_double,
	af_signed,	/* 16-bit native endian */
} af_format;

/* Returns a handle for the audio file, NULL on failure */
extern audio_file_t *open_audio_file(char *filename);

extern int audio_file_length_in_frames(audio_file_t *audio_file);

extern int audio_file_channels(audio_file_t *audio_file);

extern double audio_file_sampling_rate(audio_file_t *audio_file);

extern int read_audio_file(audio_file_t *audio_file, char *data,
			   af_format format, int channels,
			   int start,	/* In frames offset from 0.0 */
			   int nframes);

extern void close_audio_file(audio_file_t *audio_file);
