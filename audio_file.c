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

/*
 * audio_file.c - Stuff to read audio samples from a sound file
 *
 * Implemented using one of:
 * - libaudiofile, which can only read a few formats, and not ogg or mp3,
 * - libsndfile, which can read ogg but not mp3.
 * - libsox, with more formats but sox_seek() is broken for
 *	linear audio formats and mp3 gets corrupt data if you seek.
 * - libav, but only FLAC and WAV are known to work but it may decode
 *	many formats including films.
 * - libmpg123, because none of the above get MP3s right.
 *
 * Identifier names containing "audiofile" are libaudiofile library functions,
 * and those containing "audio_file" refer to this lib-independent layer.
 *
 * This also keeps track of all the opened audio files, treating them as if
 + they were one long file made of them all concatenated together and thus
 * providing audio_file_length (the sum of them all) and sample_rate (of the
 * current playing position).
 * When you read audio from some position, it converts the start position into
 * which audio file and offset into it, and reads from there. If the buffer
 * goes past the end of the current file, we get 0s at the same sample rate,
 * which is fine.
 */

#include "spettro.h"
#include "audio_file.h"		/* Our header file */

#include "audio_cache.h"
#include "convert.h"
#include "libmpg123.h"
#include "lock.h"
#include "ui.h"			/* for disp_time, disp_offset and secpp */

#include <string.h>		/* for memset() */

#if USE_LIBSNDFILE
static int mix_mono_read_floats(audio_file_t *af, float *data, int frames_to_read);
/* Buffer used by the above */
static float *multi_data = NULL;   /* buffer for incoming samples */
static int multi_data_samples = 0;  /* length of buffer in samples */
#elif USE_LIBSOX
static size_t sox_frame;	/* Which will be returned if you sox_read? */
#elif USE_LIBAV
# include "libav.h"
#endif

#if USE_LIBMPG123
#include "libmpg123.h"
#endif

static audio_file_t *audio_file = NULL;

audio_file_t *
current_audio_file(void)
{
    return audio_file;
}

/* Open the audio file to find out sampling rate, length and to be able
 * to fetch pixel data to be converted into spectra.
 *
 * Emotion seems not to let us get the raw sample data or sampling rate
 * and doesn't know the file length until the "open_done" event arrives
 * so we use libsndfile, libaudiofile or libsox for that.
 */
audio_file_t *
open_audio_file(char *filename)
{
    audio_file_t *af = Malloc(sizeof(*af));

    af->audio_buf = NULL;
    af->audio_buflen = 0;

#if USE_LIBMPG123
    /* Decode MP3's with libmpg123 */
    if (strcasecmp(filename + strlen(filename)-4, ".mp3") == 0) {
	if (!libmpg123_open(af, filename)) {
	    free(af);
	    return NULL;
	}
	create_audio_cache(af);
    } else /* Use the main audio file library */
#endif
    {

#if USE_LIBAUDIOFILE
    AFfilehandle afh;
    int comptype;	/* Compression type */

    if ((afh = afOpenFile(filename, "r", NULL)) == NULL) {
	/* By default it prints a line to stderr, which is what we want.
	 * For better error handling use afSetErrorHandler() */
	free(af);
	return NULL;
    }
    af->afh = afh;
    af->sample_rate = afGetRate(afh, AF_DEFAULT_TRACK);
    af->frames = afGetFrameCount(afh, AF_DEFAULT_TRACK);
    af->channels = afGetChannels(afh, AF_DEFAULT_TRACK);
    comptype = afGetCompression(afh, AF_DEFAULT_TRACK);
    switch(comptype) {
    char *compression;
    case AF_COMPRESSION_NONE:
	no_audio_cache(af);
	break;
    default:
	compression = afQueryPointer(AF_QUERYTYPE_COMPRESSION, AF_QUERY_NAME,
				     comptype, 0, 0);
	if (strcmp(compression, "FLAC") != 0) {
	    fprintf(stderr, "Unknown compression type \"%s\"\n", compression);
	}
	create_audio_cache(af);
	break;
    }

#elif USE_LIBSNDFILE
    SF_INFO info;
    SNDFILE *sndfile;

    memset(&info, 0, sizeof(info));

    if ((sndfile = sf_open(filename, SFM_READ, &info)) == NULL) {
	free(af);
	return NULL;
    }
    af->sndfile = sndfile;
    af->sample_rate = info.samplerate;
    af->frames = info.frames;
    af->channels = info.channels;
    /* Switch on major format type */
    switch (info.format & 0xFFFF0000) {
    case SF_FORMAT_FLAC:
    case SF_FORMAT_OGG:
	create_audio_cache(af);
	break;
    default:	/* All other formats are uncompressed */
	no_audio_cache(af);
	break;
    }

#elif USE_LIBSOX
    sox_format_t *sf;

    sox_init();
    sox_format_init();
    sf = sox_open_read(filename, NULL, NULL, NULL);
    if (sf == NULL) {
	free(af);
	return NULL;
    }
    af->sf = sf;
    af->sample_rate = sf->signal.rate;
    af->channels = sf->signal.channels;
    af->frames = sf->signal.length / af->channels;
    switch (sf->encoding.encoding) {	/* See sox.h */
    case SOX_ENCODING_FLAC:
    case SOX_ENCODING_MP3:       /**< MP3 compression */
    case SOX_ENCODING_VORBIS:    /**< Vorbis compression */
	create_audio_cache(af);
	break;
    default:	/* linear type - do not cache */
	no_audio_cache(af);
	break;
    }
    sox_frame = 0;

#elif USE_LIBAV
    libav_open_audio_file(&af, filename);
    if (af == NULL) {
	free(af);
	return NULL;
    }
    create_audio_cache(af);
#endif

    }

    af->filename = filename;

    audio_file = af;
    return af;
}

/* Return the length of an audio file in seconds. */
double
audio_file_length(void)
{
    return audio_file == NULL ? 0 :
	   (double)(audio_file->frames) / audio_file->sample_rate;
}

/* Map a playing time to the offset in seconds into the audio file.
 *
 * If the time is within the audio file, it fills in the last parameter
 * with the offset into it and returns TRUE.
 * If the time is outside the audio, it returns FALSE.
 */
bool
time_to_offset(double when, double *offset_p)
{
    double t = when;

    /* If it's less than the length of this piece, this is the file. */
    if (DELTA_GE(t, 0.0) && DELTA_LE(t, audio_file_length())) {
	if (offset_p) *offset_p = t;
	return TRUE;
    }

    return FALSE;
}

/* A similar function to convert a screen column to the same */
bool
col_to_offset(int col, double *offset_p)
{
    return time_to_offset(screen_column_to_start_time(col), offset_p);
}

/* What is the sample rate of the audio file? */
double
current_sample_rate()
{
    if (!col_to_offset(disp_offset, NULL)) {
	fprintf(stderr, "Warning: Cannot find current sample rate\n");
	return 0.0;
    }

    if (audio_file == NULL) {
	fprintf(stderr, "Internal error: requested sample rate with no audio file\n");
	abort();
    }

    return audio_file->sample_rate;
}

/*
 * read_audio_file(): Read sample frames from the audio file,
 * returning them as mono floats for the graphics or with the
 * original number of channels as 16-bit system-native bytendianness
 * for the sound player.
 *
 * "data" is where to put the audio data.
 * "format" is one of af_float or af_signed
 * "channels" is the number of desired channels, 1 to monoise or copied from
 *		the WAV file to play as-is.
 * "start" is the index of the first sample frame to read.
 *	It may be negative if we are reading data for FFT transformation,
 *	in which case we invent some 0 data for the leading silence.
 * "frames_to_read" is the number of multi-sample frames to fill "data" with.
 */

#if USE_LIBSOX
/* Sox gives us the samples as 32-bit signed integers so
 * accept them that way into a private buffer and then convert them
 */
static sox_sample_t *sox_buf = NULL;
static size_t sox_buf_size = 0;	/* Size of sox_buf in samples */
#endif

int
read_audio_file(char *data,
		af_format_t format, int channels,
		int start, int frames_to_read)
{
#if USE_LIBAUDIOFILE
    AFfilehandle afh = audio_file->afh;
#elif USE_LIBSNDFILE
    SNDFILE *sndfile = audio_file->sndfile;
#elif USE_LIBSOX
    sox_format_t *sf = audio_file->sf;
    int samples_to_read, samples;
#endif

    /* size of one frame of output data in bytes */
    int framesize = (format == af_float ? sizeof(float) : sizeof(short))
		    * channels;
    int total_frames = 0;	/* How many frames have we filled? */
    char *write_to = data;	/* Where to write next data */

#if USE_LIBAUDIOFILE
    if (afSetVirtualSampleFormat(afh, AF_DEFAULT_TRACK,
	format == af_float ? AF_SAMPFMT_FLOAT : AF_SAMPFMT_TWOSCOMP,
	format == af_float ? sizeof(float) : sizeof(short)) ||
        afSetVirtualChannels(afh, AF_DEFAULT_TRACK, channels)) {
            fprintf(stderr, "Can't set virtual sample format.\n");
	    return 0;
    }
#endif

    if (start < 0) {
	if (format != af_float) {
	    fprintf(stderr, "Internal error: Reading audio data for playing from before the start of the piece");
	    exit(1);
	}
	/* Fill before time 0.0 with silence */
        int silence = -start;	/* How many silent frames to fill */
        memset(write_to, 0, silence * framesize);
        write_to += silence * framesize;
	total_frames += silence;
        frames_to_read -= silence;
	start = 0;	/* Read audio data from start of file */
    }

#if USE_LIBMPG123
    /* Decode MP3's with libmpg123 */
    if (strcasecmp(audio_file->filename + strlen(audio_file->filename) - 4,
    		   ".mp3") == 0) {
	if (libmpg123_seek(audio_file, start) == FALSE) {
	    fprintf(stderr, "Failed to seek in audio file.\n");
	    return 0;
	}
	while (frames_to_read > 0) {
	    int frames = libmpg123_read_frames(audio_file, write_to, frames_to_read, format, NULL, NULL, NULL);
	    if (frames > 0) {
		total_frames += frames;
		write_to += frames * framesize;
		frames_to_read -= frames;
	    } else {
		/* We ask it to read past EOF so failure is normal */
		break;
	    }
	}
    } else
#endif

    {

#if USE_LIBSOX
    /* sox_seek() (in libsox-14.4.1) is broken:
     * if you seek to an earlier position it does nothing
     * and just returns the data linearly to end of file.
     * Work round this by closing and reopening the file.
     */
    if (start < sox_frame) {
	sox_close(sf);
	sf = sox_open_read(audio_file->filename, NULL, NULL, NULL);
	sox_frame = 0;
	if (sf == NULL) {
	    fprintf(stderr, "libsox failed to reopen \"%s\"\n", audio_file->filename);
	    return 0;
	}
	audio_file->sf = sf;
    }
#endif

    if (
#if USE_LIBAUDIOFILE
        afSeekFrame(afh, AF_DEFAULT_TRACK, start) != start
#elif USE_LIBSNDFILE
        sf_seek(sndfile, start, SEEK_SET) != start
#elif USE_LIBSOX
	/* sox seeks in samples, not frames */
	sox_seek(sf, start * audio_file->channels, SOX_SEEK_SET) != 0
#elif USE_LIBAV
	libav_seek(start) != 0
#endif
	) {
	fprintf(stderr, "Failed to seek in audio file.\n");
	return 0;
    }

#if USE_LIBSOX
    sox_frame = start;	/* Next audio should be returned from this offset */

    /* sox reads a number of samples, not frames */
    samples_to_read = frames_to_read * audio_file->channels;

    /* Adjust size of 32-bit-sample buffer for the raw data from sox_read() */
    if (sox_buf_size < samples_to_read) {
	sox_buf = Realloc(sox_buf, samples_to_read * sizeof(sox_sample_t));
	sox_buf_size = samples_to_read;
    }
#endif

    /* Read from the file until we have read all requested samples */
    while (frames_to_read > 0) {
        int frames;	/* How many frames did the last read() call return? */

	/* libaudio and libsndfile's sample frames are a sample for each channel */
#if USE_LIBAUDIOFILE

	/* libaudiofile does the mixing down to one channel for floats */
        frames = afReadFrames(afh, AF_DEFAULT_TRACK, write_to, frames_to_read);

#elif USE_LIBSNDFILE

	if (format == af_float) {
            frames = mix_mono_read_floats(audio_file,
					   (float *)write_to, frames_to_read);
	} else {
	    if (channels != audio_file->channels) {
		fprintf(stderr, "Wrong number of channels in signed audio read!\n");
		return 0;
	    }
	    frames = sf_readf_short(sndfile, (short *)write_to, frames_to_read);
	}

#elif USE_LIBSOX

	/* Shadows of write_to with an appropriate type */
    	float *fp;
    	signed short *sp;

	samples_to_read = frames_to_read * audio_file->channels;
	samples = sox_read(sf, sox_buf, samples_to_read);
	if (samples == SOX_EOF)  {
	    frames = 0;
	}
	frames = samples / audio_file->channels;
	if (frames == 0) break;

	sox_frame += frames;

	/* Convert to desired format */
	switch (format) {
	case af_float:	/* Mono floats for FFT */
	    if (channels != 1) {
		fprintf(stderr, "Asking for float audio with %d channels",
			channels);
		return 0;
	    }

    	    fp = (float *) write_to;
	    /* Convert mono values to float */
	    if (audio_file->channels == 1) {
		/* Convert mono samples to floats */
		sox_sample_t *bp = sox_buf;	/* Where to read from */
		int clips = 0;
		int i;

		(void)clips;	/* Disable "unused variable" warning */
		for (i=0; i<samples; i++) {
		    *fp++ = SOX_SAMPLE_TO_FLOAT_64BIT(*bp++, clips);
		}
	    } else {
		/* Convert multi-sample frames to mono floats */
		sox_sample_t *bp = sox_buf;
		int i;

		for (i=0; i<frames; i++) {
		    int channel;
		    int clips = 0;

		    *fp = 0.0;
		    for (channel=0; channel < audio_file->channels; channel++)
			*fp += SOX_SAMPLE_TO_FLOAT_64BIT(*bp++, clips);
		    *fp++ /= audio_file->channels;
		    (void)clips;	/* Disable "unused variable" warning */
		}
	    }
	    break;
	case af_signed:	/* As-is for playing */
	    if (audio_file->channels != channels) {
		fprintf(stderr, "Asking for signed audio with %d channels",
			channels);
		return 0;
	    }
	    {
		sox_sample_t *bp = sox_buf;
		int i;

    	        sp = (signed short *) write_to;
		for (i=0; i<samples; i++) {
		    sox_sample_t sox_macro_temp_sample;
		    double sox_macro_temp_double;
		    int clips = 0;

		    *sp++ = SOX_SAMPLE_TO_SIGNED(16, *bp++, clips);
		    (void)clips;	/* Disable "unused variable" warning */
		}
	    }
	    break;
	default:
	   fprintf(stderr, "Internal error: Unknown sample format.\n");
	   abort();
	}

#elif USE_LIBAV

	frames = libav_read_frames(write_to, frames_to_read, format);

#endif

        if (frames > 0) {
	    total_frames += frames;
            write_to += frames * framesize;
            frames_to_read -= frames;
        } else {
            /* We ask it to read past EOF so failure is normal */
	    break;
        }
    }

    }

    /* If it stopped before reading all frames, fill the rest with silence */
    if (format == af_float && frames_to_read > 0) {
        memset(write_to, 0, frames_to_read * framesize);
	total_frames += frames_to_read;
	frames_to_read = 0;
    }

    return total_frames;
}

void
close_audio_file(audio_file_t *af)
{
    if (af == NULL) return;

#if USE_LIBAUDIOFILE
    afCloseFile(af->afh);
#elif USE_LIBSNDFILE
    sf_close(af->sndfile);
    free(multi_data);
#elif USE_LIBSOX
    sox_close(af->sf);
    free(sox_buf);
#elif LIBAV
    libav_close();
#endif
    free(af);
}

#if USE_LIBSNDFILE
/* This last function is from sndfile-tools */

/*
** Copyright (C) 2007-2015 Erik de Castro Lopo <erikd@mega-nerd.com>
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 or version 3 of the
** License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define ARRAY_LEN(x)	((int) (sizeof (x) / sizeof (x [0])))

#define MAX(x, y)		((x) > (y) ? (x) : (y))
#define MIN(x, y)		((x) < (y) ? (x) : (y))

static int
mix_mono_read_floats(audio_file_t *af, float *data, int frames_to_read)
{
    if (af->channels == 1)
#if USE_LIBAUDIOFILE
	return afReadFrames(af->afh, AF_DEFAULT_TRACK, data, frames_to_read);
#elif USE_LIBSNDFILE
	return sf_read_float(af->sndfile, data, frames_to_read);
#endif

    /* Read multi-channel data and mix it down to a single channel of floats */
    {
	int k, ch, frames_read;
	int dataout = 0;		    /* No of samples written so far */

	if (multi_data_samples < frames_to_read * af->channels) {
	    multi_data = Realloc(multi_data, frames_to_read * af->channels * sizeof(*multi_data));
	    multi_data_samples = frames_to_read * af->channels;
	}

	while (dataout < frames_to_read) {
	    /* Number of frames to read from file */
	    int this_read = frames_to_read - dataout;

#if USE_LIBAUDIOFILE
	    /* A libaudiofile frame is a sample for each channel */
	    frames_read = afReadFrames(af->afh, AF_DEFAULT_TRACK, multi_data, this_read);
#elif USE_LIBSNDFILE
	    /* A sf_readf_float frame is a sample for each channel */
	    frames_read = sf_readf_float(af->sndfile, multi_data, this_read);
#endif
	    if (frames_read <= 0)
		break;

	    for (k = 0; k < frames_read; k++) {
		float mix = 0.0;

		for (ch = 0; ch < af->channels; ch++)
		    mix += multi_data[k * af->channels + ch];
		data[dataout + k] = mix / af->channels;
	    }

	    dataout += frames_read;
	}

	return dataout;
  }
} /* mix_mono_read_float */
#endif
