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
