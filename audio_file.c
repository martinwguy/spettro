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
#include "ui.h"			/* for disp_time, disp_offset and step */

#include <string.h>		/* for memset() */

#if USE_LIBSNDFILE
static int mix_mono_read_doubles(audio_file_t *af, double *data, int frames_to_read);
/* Buffer used by the above */
static double *multi_data = NULL;   /* buffer for incoming samples */
static int multi_data_samples = 0;  /* length of buffer in samples */
#elif USE_LIBSOX
static size_t sox_frame;	/* Which will be returned if you sox_read? */
#elif USE_LIBAV
# include "libav.h"
#endif

#if USE_LIBMPG123
#include "libmpg123.h"
#endif

static audio_file_t *audio_files = NULL;

/* Pointer to the "next" field of the last cell in the list */
static audio_file_t **last_audio_file_nextp = &audio_files;

audio_file_t *
open_audio_file(char *filename)
{
    audio_file_t *af = Malloc(sizeof(*af));

    af->cache = -1;
    af->audio_buf = NULL;
    af->audio_buflen = 0;

#if USE_LIBMPG123
    /* Decode MP3's with libmpg123 */
    if (strcasecmp(filename + strlen(filename)-4, ".mp3") == 0) {
	if (!libmpg123_open(af, filename)) {
	    fprintf(stderr, "Cannot open \"%s\n", filename);
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
	fprintf(stderr, "Cannot open \"%s\n", filename);
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
	fprintf(stderr, "libsndfile failed to open \"%s\": %s\n",
		filename, sf_strerror(NULL));
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
	fprintf(stderr, "Cannot open \"%s\n", filename);
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
	fprintf(stderr, "Cannot open \"%s\n", filename);
	return NULL;
    }
    create_audio_cache(af);
#endif

    }

    af->filename = filename;

    /* Add the audio file to the list of audio files opened so far */
    af->next = NULL;
    *last_audio_file_nextp = af;
    last_audio_file_nextp = &(af->next);

    return af;
}

/* Return the length of an audio file in seconds. */
double
audio_file_length(audio_file_t *af)
{
    return (double)(af->frames) / af->sample_rate;
}

/* Return the length of all the audio files */
double
audio_files_length()
{
    audio_file_t *afp;
    double audio_length = 0.0;

    for (afp = audio_files; afp != NULL; afp = afp->next)
	audio_length += audio_file_length(afp);

    return audio_length;
}

/* Map a playing time in the whole piece (all audio files concatenated)
 * to the specific audio file and the offset in seconds into it.
 *
 * If all goes well, it fills in the last two parameters with the audio file
 * and offset into it and returns TRUE.  If not, it returns FALSE.
 */
bool
time_to_af_and_offset(double when,
		      audio_file_t **audio_file_p, double *offset_p)
{
    audio_file_t *af;
    audio_file_t *last_audio_file = audio_files;
    double t = when;
    double length = 0.0;	/* Length of *af, left by the loop as
    				 * the length of last piece */

    for (af = audio_files; af != NULL; af = af->next) {
    	length = (double)(af->frames) / af->sample_rate;

	/* If it's less than the length of this piece, this is the file.
	 * If it is equal to the length of an audio file, that's the same as
	 * the start of the following one. */
	if (DELTA_LT(t, length)) {
	    if (audio_file_p)	*audio_file_p = af;
	    if (offset_p)	*offset_p = t;
	    return TRUE;
	}
	t -= length;
	last_audio_file = af;
    }

    /* Special case: when "when" is the total overall length of all audio,
     * i.e. at the end of the last file */
    if (DELTA_EQ(t, 0.0)) {
    	if (audio_file_p)	*audio_file_p = last_audio_file;
	if (offset_p)		*offset_p = length;
	return TRUE;
    }

    return FALSE;
}

/* A similar function to convert a screen column to the same */
bool
col_to_af_and_offset(int col, audio_file_t **audio_file_p, double *offset_p)
{
    return time_to_af_and_offset(screen_column_to_start_time(col),
    				 audio_file_p, offset_p);
}

/* What is the sample rate of the audio file at the current playing postion? */
double
current_sample_rate()
{
    audio_file_t *af;

    if (!col_to_af_and_offset(disp_offset, &af, NULL)) {
	fprintf(stderr, "Cannot find current sample rate\n");
	return 0.0;
    }

    return af->sample_rate;
}

/*
 * read_audio_file(): Read sample frames from the audio file,
 * returning them as mono doubles for the graphics or with the
 * original number of channels as 16-bit system-native bytendianness
 * for the sound player.
 *
 * "data" is where to put the audio data.
 * "format" is one of af_double or af_signed
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
read_audio_file(audio_file_t *af, char *data,
		af_format_t format, int channels,
		int start, int frames_to_read)
{
#if USE_LIBAUDIOFILE
    AFfilehandle afh = af->afh;
#elif USE_LIBSNDFILE
    SNDFILE *sndfile = af->sndfile;
#elif USE_LIBSOX
    sox_format_t *sf = af->sf;
    int samples_to_read, samples;
#endif

    /* size of one frame of output data in bytes */
    int framesize = (format == af_double ? sizeof(double) : sizeof(short))
		    * channels;
    int total_frames = 0;	/* How many frames have we filled? */
    char *write_to = data;	/* Where to write next data */

#if USE_LIBAUDIOFILE
    if (afSetVirtualSampleFormat(afh, AF_DEFAULT_TRACK,
	format == af_double ? AF_SAMPFMT_DOUBLE : AF_SAMPFMT_TWOSCOMP,
	format == af_double ? sizeof(double) : sizeof(short)) ||
        afSetVirtualChannels(afh, AF_DEFAULT_TRACK, channels)) {
            fprintf(stderr, "Can't set virtual sample format.\n");
	    return 0;
    }
#endif

    if (start < 0) {
	if (format != af_double) {
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
    if (strcasecmp(af->filename + strlen(af->filename) - 4,
    		   ".mp3") == 0) {
	if (libmpg123_seek(af, start) == FALSE) {
	    fprintf(stderr, "Failed to seek in audio file.\n");
	    return 0;
	}
	while (frames_to_read > 0) {
	    int frames = libmpg123_read_frames(af, write_to, frames_to_read, format, NULL, NULL, NULL);
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
	sf = sox_open_read(af->filename, NULL, NULL, NULL);
	sox_frame = 0;
	if (sf == NULL) {
	    fprintf(stderr, "libsox failed to reopen \"%s\"\n", af->filename);
	    return 0;
	}
	af->sf = sf;
    }
#endif

    if (
#if USE_LIBAUDIOFILE
        afSeekFrame(afh, AF_DEFAULT_TRACK, start) != start
#elif USE_LIBSNDFILE
        sf_seek(sndfile, start, SEEK_SET) != start
#elif USE_LIBSOX
	/* sox seeks in samples, not frames */
	sox_seek(sf, start * af->channels, SOX_SEEK_SET) != 0
#elif USE_LIBAV
	libav_seek(start) != 0
#endif
	) {
	fprintf(stderr, "Failed to seek in audio file.\n");
	return 0;
    }

#if USE_LIBSOX
    sox_frame = start;	/* Next audio should be truened from this offset */

    /* sox reads a number of samples, not frames */
    samples_to_read = frames_to_read * af->channels;

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

	/* libaudiofile does the mixing down to one channel for doubles */
        frames = afReadFrames(afh, AF_DEFAULT_TRACK, write_to, frames_to_read);

#elif USE_LIBSNDFILE

	if (format == af_double) {
            frames = mix_mono_read_doubles(af,
					   (double *)write_to, frames_to_read);
	} else {
	    if (channels != af->channels) {
		fprintf(stderr, "Wrong number of channels in signed audio read!\n");
		return 0;
	    }
	    frames = sf_readf_short(sndfile, (short *)write_to, frames_to_read);
	}

#elif USE_LIBSOX

	/* Shadows of write_to with an appropriate type */
    	double *dp;
    	signed short *sp;

	samples_to_read = frames_to_read * af->channels;
	samples = sox_read(sf, sox_buf, samples_to_read);
	if (samples == SOX_EOF)  {
	    frames = 0;
	}
	frames = samples / af->channels;
	if (frames == 0) break;

	sox_frame += frames;

	/* Convert to desired format */
	switch (format) {
	case af_double:	/* Mono doubles for FFT */
	    if (channels != 1) {
		fprintf(stderr, "Asking for double audio with %d channels",
			channels);
		return 0;
	    }

    	    dp = (double *) write_to;
	    /* Convert mono values to double */
	    if (af->channels == 1) {
		/* Convert mono samples to doubles */
		sox_sample_t *bp = sox_buf;	/* Where to read from */
		int clips = 0;
		int i;

		(void)clips;	/* Disable "unused variable" warning */
		for (i=0; i<samples; i++) {
		    *dp++ = SOX_SAMPLE_TO_FLOAT_64BIT(*bp++, clips);
		}
	    } else {
		/* Convert multi-sample frames to mono doubles */
		sox_sample_t *bp = sox_buf;
		int i;

		for (i=0; i<frames; i++) {
		    int channel;
		    int clips = 0;

		    *dp = 0.0;
		    for (channel=0; channel < af->channels; channel++)
			*dp += SOX_SAMPLE_TO_FLOAT_64BIT(*bp++, clips);
		    *dp++ /= af->channels;
		    (void)clips;	/* Disable "unused variable" warning */
		}
	    }
	    break;
	case af_signed:	/* As-is for playing */
	    if (af->channels != channels) {
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
    if (format == af_double && frames_to_read > 0) {
        memset(write_to, 0, frames_to_read * framesize);
	total_frames += frames_to_read;
	frames_to_read = 0;
    }

    return total_frames;
}

void
close_audio_file(audio_file_t *af)
{
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
mix_mono_read_doubles(audio_file_t *af, double *data, int frames_to_read)
{
    if (af->channels == 1)
#if USE_LIBAUDIOFILE
	return afReadFrames(af->afh, AF_DEFAULT_TRACK, data, frames_to_read);
#elif USE_LIBSNDFILE
	return sf_read_double(af->sndfile, data, frames_to_read);
#endif

    /* Read multi-channel data and mix it down to a single channel of doubles */
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
	    /* A sf_readf_double frame is a sample for each channel */
	    frames_read = sf_readf_double(af->sndfile, multi_data, this_read);
#endif
	    if (frames_read <= 0)
		break;

	    for (k = 0; k < frames_read; k++) {
		double mix = 0.0;

		for (ch = 0; ch < af->channels; ch++)
		    mix += multi_data[k * af->channels + ch];
		data[dataout + k] = mix / af->channels;
	    }

	    dataout += frames_read;
	}

	return dataout;
  }
} /* mix_mono_read_double */
#endif
