/* libmpg123.h: Interface to libmpg123.c */

#include "audio_file.h"		/* for af_format_t */

extern bool libmpg123_open(audio_file_t *af, const char *filename);
extern bool libmpg123_seek(audio_file_t *af, int start);
extern int  libmpg123_read_frames(audio_file_t	*af,
				  void		*write_to,
				  int		frames_to_read,
				  af_format_t	format,
				  double	*sample_rate_p,
				  unsigned	*channels_p,
				  unsigned long	*frames_p);
extern void libmpg123_close(audio_file_t *af);
