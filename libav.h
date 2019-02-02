/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Declarations for callers of libav.c */

#ifndef LIBAV_H
#define LIBAV_H 1

# if USE_LIBAV
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

extern int open_input_file(const char *filename);
extern int init_filters(const char *filters_descr);
extern void print_frame(const AVFrame *frame);
extern int filtering_main(int argc, char **argv);

#include "audio_file.h"
extern void libav_open_audio_file(audio_file_t **afp, char *filename);
extern int *libav_seek(int start);
extern int libav_read_frames(void *write_to, int nframes, af_format_t format);
extern void libav_close(void);

# endif

#endif
