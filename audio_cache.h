/* Declarations for audio_cache.c */

#include "audio_file.h"	/* for af_format_t and audio_file_t */

extern void create_audio_cache(void);	/* Use a cache on the audio file */
extern void no_audio_cache(void);	/* Don't use cache on audio file,
					 * also, free the memory the cache
					 * is using if it was created */

/* Read audio from the audio file through the cache.
 * Same interface as read_audio_file
 */
extern int read_cached_audio(audio_file_t *audio_file, char *data,
			     af_format_t format, int channels,
			     int start, int frames_to_read);
