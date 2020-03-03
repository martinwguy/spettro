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

/* libav.c: Functions to read audio files using ffmpeg's libavformat library
 *
 * Based on the example program doc/examples/filtering_audio.c from ffmpeg-3.3.8
 */

#include "spettro.h"
#include "audio_file.h"

#if USE_LIBAV
/* ================== Code from ffmpeg-3.3.8 ========================== */
/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2012 Clément Bœsch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * API example for audio decoding and filtering
 * @example filtering_audio.c
 */

#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
static AVFilterContext *buffersink_ctx;
static AVFilterContext *buffersrc_ctx;
static AVFilterGraph *filter_graph;
static int audio_stream_index = -1;

int open_input_file(const char *filename)
{
    int ret;
    AVCodec *dec;

    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    }

    /* select the audio stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find an audio stream in the input file\n");
        return ret;
    }
    audio_stream_index = ret;

    /* create decoding context */
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[audio_stream_index]->codecpar);
    av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);

    /* init the audio decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open audio decoder\n");
        return ret;
    }

/*
fprintf(stderr, "AVCodecContext: sample_rate=%d channels=%d frame_size=%d\n",
	dec_ctx->sample_rate, dec_ctx->channels, dec_ctx->frame_size);
 */

    return 0;
}

int init_filters(const char *filters_descr, int sample_rate, af_format_t format)
{
    char args[512];
    int ret = 0;
    const AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    static enum AVSampleFormat out_sample_fmts[] = { AV_SAMPLE_FMT_S16, -1 };
    static int64_t out_channel_layouts[] = { AV_CH_LAYOUT_MONO, -1 };
    static int out_sample_rates[] = { 8000, -1 };
    const AVFilterLink *outlink;
    AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Fill in filter parameters for what we need */
    out_sample_rates[0] = sample_rate;

    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!dec_ctx->channel_layout)
        dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
    snprintf(args, sizeof(args),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             time_base.num, time_base.den, dec_ctx->sample_rate,
             av_get_sample_fmt_name(dec_ctx->sample_fmt), dec_ctx->channel_layout);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, abuffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        goto end;
    }

    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "sample_fmts", out_sample_fmts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "channel_layouts", out_channel_layouts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
        goto end;
    }

    ret = av_opt_set_int_list(buffersink_ctx, "sample_rates", out_sample_rates, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, outlink->channel_layout);

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

void print_frame(const AVFrame *frame)
{
    const int n = frame->nb_samples * av_get_channel_layout_nb_channels(
#if LIBAVFORMAT_VERSION_INT >= 0x040000
	frame->channel_layout
#else
	/* Deprecated since 4.0.0 */
    	av_frame_get_channel_layout(frame)
#endif
    );
    const uint16_t *p     = (uint16_t*)frame->data[0];
    const uint16_t *p_end = p + n;

    while (p < p_end) {
        fputc(*p    & 0xff, stdout);
        fputc(*p>>8 & 0xff, stdout);
        p++;
    }
    fflush(stdout);
}

/*
 * This is not used by spettro. It is used when building the "filtering_audio"
 * standalone test program, which should work the same as the example program
 * that we copied the above (and this) from.
 */
int filtering_main(int argc, char **argv)
{
    int ret;
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    const char *filter_descr =
    		"aresample=8000,aformat=sample_fmts=s16:channel_layouts=mono";
    const char *player = "ffplay -f s16le -ar 8000 -ac 1 -";

    if (!frame || !filt_frame) {
        perror("Could not allocate frame");
        exit(1);
    }
    if (argc != 2) {
        fprintf(stderr, "Usage: %s file | %s\n", argv[0], player);
        exit(1);
    }

#if LIBAVFORMAT_VERSION_INT < 0x040000
    /* Deprecated and not required from 4.0.0 */
    av_register_all();
    avfilter_register_all();
#endif

    if ((ret = open_input_file(argv[1])) < 0)
        goto end;
    if ((ret = init_filters(filter_descr, 44100, af_signed)) < 0)
        goto end;

    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
            break;

        if (packet.stream_index == audio_stream_index) {
            ret = avcodec_send_packet(dec_ctx, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                if (ret >= 0) {
                    /* push the audio data from decoded frame into the filtergraph */
                    if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                        av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
                        break;
                    }

                    /* pull filtered audio from the filtergraph */
                    while (1) {
                        ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                            break;
                        if (ret < 0)
                            goto end;
                        print_frame(filt_frame);
                        av_frame_unref(filt_frame);
                    }
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(&packet);
    }
end:
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }

    exit(0);
}

/* =================Our code begins======================= */

/*	From here on copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
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

#include "audio_file.h"

#if LIBAVFORMAT_VERSION_INT < 0x040000
static bool libav_is_initialized = FALSE;
#endif
static int libav_stream_index;

/* Open the audio file and fill in fields of the supplied audio_file_t.
 * On failure, set their audio file pointer to NULL
 */
void
libav_open_audio_file(audio_file_t **afp, const char *filename)
{
    audio_file_t *af = *afp;
    char *filter_descr;

#if LIBAVFORMAT_VERSION_INT < 0x040000
    if (!libav_is_initialized) {
        av_register_all();
	avfilter_register_all();
        libav_is_initialized = TRUE;
    }
#endif

    libav_stream_index = open_input_file(filename);
    if (libav_stream_index < 0) goto fail;

    /* format = af_single ? "aformat=sample_fmts=s16"
			  : "aformat=sample_fmts=dbl:channel_layouts=mono"; */
    filter_descr = "aformat=sample_fmts=s16";
    if (init_filters(filter_descr, dec_ctx->sample_rate, af_signed) != 0) goto fail;

    af->sample_rate = dec_ctx->sample_rate;
    af->channels = dec_ctx->channels;
    af->frames = lrint(((double)(fmt_ctx->duration) / AV_TIME_BASE)
    			       * (double)(dec_ctx->sample_rate));
 
    return;

fail:
    free(af);
    *afp = NULL;
    return;
}

/* Seek to sample frame "start".
 *
 * For uncompressed audio, it seeks to the right sample.
 * For most compressed formats, it seeks to the start of the chunk that contains
 * the sample frame, leaving "frames_to_skip" as the number of samples frames
 * to skip when reading, to get the the right sample.
 *
 * Returns 0 for success, non-zero for failure
 */

static int frames_to_skip = 0;

int
libav_seek(int start)
{
    int64_t dts;	/* Which sample frame did it actually seek to? */

    if (av_seek_frame(fmt_ctx, audio_stream_index, start, AVSEEK_FLAG_BACKWARD)
        < 0) return 1;

    dts = fmt_ctx->streams[audio_stream_index]->cur_dts;
    if (dts > start) { fprintf(stderr, "Late seek\n"); return -1; }

    /* Tell libav_read_frames() how many samples to drop at the start */
    frames_to_skip = start - dts;

    return 0;
}

/* Returns the number of frames written into "write_to" */
int
libav_read_frames(void *write_to, int frames_to_read, af_format_t format)
{
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    int frames_written = 0;
    uint16_t *sp = (uint16_t *)write_to;
    double *dp = (double *)write_to;
    int ret;

    /* In chunked audio formats, libav_seek() moves to the start of the chunk
     * containing the required offset. To get sample-precise audio we have to
     * figure out where it really went and discard the extra initial samples.
     */

    /* read enough packets */
    while (frames_written < frames_to_read) {
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
            break;

        if (packet.stream_index == audio_stream_index) {
            ret = avcodec_send_packet(dec_ctx, &packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    goto stop;
                } else if (ret < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder\n");
                    goto stop;
                }

                if (ret >= 0) {
                    /* push the audio data from decoded frame into the filtergraph */
                    if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                        av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
                        break;
                    }

                    /* pull filtered audio from the filtergraph */
                    while (1) {
                        ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                            break;
                        if (ret < 0) {
			    fprintf(stderr, "Error reading the audio file\n");
                            break;
			}
                        /* print_frame */
			{
			    const AVFrame *frame = filt_frame;
			    const int channels = av_get_channel_layout_nb_channels(
#if LIBAVFORMAT_VERSION_INT < 0x040000
				av_frame_get_channel_layout(frame)
#else
				frame->channel_layout
#endif
			    );
			    const int n = frame->nb_samples * channels;
			    const int16_t *p = (int16_t*)frame->data[0];
			    const int16_t *p_end = p + n;

			    while (p < p_end && frames_written < frames_to_read) {
			    	if (frames_to_skip > 0) {
				    p += channels;
				    frames_to_skip--;
				    continue;
				}
				switch (format) {
				case af_signed:
				    *sp++ = *p++;
				    break;
				case af_double:
				    {
					int c; double d;
					for (c=0, d=0.0; c < channels; c++)
					    d += (double)*p++ / 32767.0;
					*dp++ = d;
					break;
				    }
				}
				frames_written++;
			    }
			}
                        av_frame_unref(filt_frame);
                    }
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(&packet);
    }
stop:
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    return frames_written;
}

void
libav_close()
{
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
}

#endif
