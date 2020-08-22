/* libsndfile.h: Interface to libsndfile.c */

#ifndef LIBSNDFILE_H

#include "audio_file.h"

extern bool libsndfile_open(audio_file_t *af, char *filename);

extern bool libsndfile_seek(audio_file_t *af, int start);

extern int libsndfile_read_frames(audio_file_t *af,
				  void *write_to,
				  int frames_to_read,
				  af_format_t format);

extern void libsndfile_close(audio_file_t *af);

#define LIBSNDFILE_H 1
#endif
