/* libmpg123.h: Interface to libmpg123.c */

#include "audio_file.h"		/* for af_format_t */

extern void libmpg123_open(audio_file_t **afp, const char *filename);
extern bool libmpg123_seek(int start);
extern int  libmpg123_read_frames(void *write_to, int frames_to_read,
				  af_format_t format);
extern void libmpg123_close(void);
