/* libmpg123.h: Interface to libmpg123.c */

#include "audio_file.h"		/* for af_format_t */

extern bool libmpg123_open(const char *filename, audio_file_t *af);
extern bool libmpg123_seek(int start);
extern int  libmpg123_read_frames(void		*write_to,
				  int		frames_to_read,
				  af_format_t	format,
				  double	*sample_rate_p,
				  unsigned	*channels_p,
				  unsigned long	*frames_p);
extern void libmpg123_close(void);
