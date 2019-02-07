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
 *
 * Identifier names containing "audiofile" are libaudiofile library functions,
 * and those containing "audio_file" refer to this lib-independent layer.
 */

#include "spettro.h"
#include "audio_file.h"		/* Our header file */

#include "audio_cache.h"
#include "libmpg123.h"
#include "lock.h"

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

/* Handing the audio file info down to everybody is too much of a pain
 * so we just make it global.
 */
static audio_file_t our_audio_file;

/* Audio file info for everybody */
audio_file_t *	audio_file = &our_audio_file;

audio_file_t *
open_audio_file(char *filename)
{
#if USE_LIBMPG123
    /* Decode MP3's with libmpg123 */
    if (strcasecmp(filename + strlen(filename)-4, ".mp3") == 0) {
	libmpg123_open(&audio_file, filename);
	audio_file->filename = filename;
	return audio_file;
    } else /* Use the main audio file library */
#endif

#if USE_LIBAUDIOFILE
    {
    AFfilehandle af;
    int comptype;	/* Compression type */

    if ((af = afOpenFile(filename, "r", NULL)) == NULL) {
	/* By default it prints a line to stderr, which is what we want.
	 * For better error handling use afSetErrorHandler() */
	free(audio_file);
	return(NULL);
    }
    audio_file->af = af;
    audio_file->sample_rate = afGetRate(af, AF_DEFAULT_TRACK);
    audio_file->frames = afGetFrameCount(af, AF_DEFAULT_TRACK);
    audio_file->channels = afGetChannels(af, AF_DEFAULT_TRACK);
    comptype = afGetCompression(af, AF_DEFAULT_TRACK);
    switch(comptype) {
    char *compression;
    case AF_COMPRESSION_NONE:
	no_audio_cache();
	break;
    default:
	compression = afQueryPointer(AF_QUERYTYPE_COMPRESSION, AF_QUERY_NAME,
				     comptype, 0, 0);
	if (strcmp(compression, "FLAC") != 0) {
	    fprintf(stderr, "Unknown compression type \"%s\"\n", compression);
	}
	create_audio_cache();
	break;
    }
    }

#elif USE_LIBSNDFILE
    {
    SF_INFO info;
    SNDFILE *sndfile;

    memset(&info, 0, sizeof(info));

    if ((sndfile = sf_open(filename, SFM_READ, &info)) == NULL) {
	fprintf(stderr, "libsndfile failed to open \"%s\": %s\n",
		filename, sf_strerror(NULL));
	return(NULL);
    }
    audio_file->sndfile = sndfile;
    audio_file->sample_rate = info.samplerate;
    audio_file->frames = info.frames;
    audio_file->channels = info.channels;
    /* Switch on major format type */
    switch (info.format & 0xFFFF0000) {
    case SF_FORMAT_FLAC:
    case SF_FORMAT_OGG:
	create_audio_cache();
	break;
    default:	/* All other formats are uncompressed */
	no_audio_cache();
	break;
    }
    }

#elif USE_LIBSOX
    {
    sox_format_t *sf;

    sox_init();
    sox_format_init();
    sf = sox_open_read(filename, NULL, NULL, NULL);
    if (sf == NULL) {
	fprintf(stderr, "libsox failed to open \"%s\"\n", filename);
	return(NULL);
    }
    audio_file->sf = sf;
    audio_file->sample_rate = sf->signal.rate;
    audio_file->channels = sf->signal.channels;
    audio_file->frames = sf->signal.length / audio_file->channels;
    switch (sf->encoding.encoding) {	/* See sox.h */
    case SOX_ENCODING_FLAC:
    case SOX_ENCODING_MP3:       /**< MP3 compression */
    case SOX_ENCODING_VORBIS:    /**< Vorbis compression */
	create_audio_cache();
	break;
    default:	/* linear type - do not cache */
	no_audio_cache();
	break;
    }
    sox_frame = 0;
    }

#elif USE_LIBAV
    libav_open_audio_file(&audio_file, filename);
    if (audio_file == NULL) {
	fprintf(stderr, "libav failed to open \"%s\"\n", filename);
	return(NULL);
    }
    create_audio_cache();
#endif

    audio_file->filename = filename;

    return(audio_file);
}

/* Return audio file length in seconds */
double
audio_file_length(audio_file_t *audio_file)
{
    return audio_file->frames / audio_file->sample_rate;
}

/*
 * Read sample frames from the audio file, returning them as mono doubles
 * for the graphics or as the original audio as 16-bit in system-native
 * bytendianness for the sound.
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
read_audio_file(audio_file_t *audio_file, char *data,
		af_format_t format, int channels,
		int start, int frames_to_read)
{
#if USE_LIBAUDIOFILE
    AFfilehandle af = audio_file->af;
#elif USE_LIBSNDFILE
    SNDFILE *sndfile = audio_file->sndfile;
#elif USE_LIBSOX
    sox_format_t *sf = audio_file->sf;
    int samples_to_read, samples;
#endif

    /* size of one frame of output data in bytes */
    int framesize = (format == af_double ? sizeof(double) : sizeof(short))
		    * channels;
    int total_frames = 0;	/* How many frames have we filled? */
    char *write_to = data;	/* Where to write next data */

#if USE_LIBAUDIOFILE
    if (afSetVirtualSampleFormat(af, AF_DEFAULT_TRACK,
	format == af_double ? AF_SAMPFMT_DOUBLE : AF_SAMPFMT_TWOSCOMP,
	format == af_double ? sizeof(double) : sizeof(short)) ||
        afSetVirtualChannels(af, AF_DEFAULT_TRACK, channels)) {
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
    if (strcasecmp(audio_file->filename + strlen(audio_file->filename) - 4,
    		   ".mp3") == 0) {
	if (libmpg123_seek(start) == FALSE) {
	    fprintf(stderr, "Failed to seek in audio file.\n");
	    return 0;
	}
	while (frames_to_read > 0) {
	    int frames = libmpg123_read_frames(write_to, frames_to_read, format);
	    if (frames > 0) {
		total_frames += frames;
		write_to += frames * framesize;
		frames_to_read -= frames;
	    } else {
		/* We ask it to read past EOF so failure is normal */
		break;
	    }
	}
    } else {
#endif

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
        afSeekFrame(af, AF_DEFAULT_TRACK, start) != start
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
    sox_frame = start;	/* Next audio should be truened from this offset */

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

	/* libaudiofile does the mixing down to one channel for doubles */
        frames = afReadFrames(af, AF_DEFAULT_TRACK, write_to, frames_to_read);

#elif USE_LIBSNDFILE

	if (format == af_double) {
            frames = mix_mono_read_doubles(audio_file,
					   (double *)write_to, frames_to_read);
	} else {
	    if (channels != audio_file->channels) {
		fprintf(stderr, "Wrong number of channels in signed audio read!\n");
		return 0;
	    }
	    frames = sf_readf_short(sndfile, (short *)write_to, frames_to_read);
	}

#elif USE_LIBSOX

	/* Shadows of write_to with an appropriate type */
    	double *dp;
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
	case af_double:	/* Mono doubles for FFT */
	    if (channels != 1) {
		fprintf(stderr, "Asking for double audio with %d channels",
			channels);
		return 0;
	    }

    	    dp = (double *) write_to;
	    /* Convert mono values to double */
	    if (audio_file->channels == 1) {
		/* Convert mono samples to doubles */
		sox_sample_t *bp = sox_buf;	/* Where to read from */
		int clips = 0;
		int i;

		(void)clips;	/* Disable "unused variable" warning */
		for (i=0; i<samples; i++) {
		    *dp++ = SOX_SAMPLE_TO_FLOAT_64BIT(*bp++,clips);
		}
	    } else {
		/* Convert multi-sample frames to doubles */
		sox_sample_t *bp = sox_buf;
		int i;

		for (i=0; i<frames; i++) {
		    int channel;
		    int clips = 0;

		    (void)clips;	/* Disable "unused variable" warning */
		    *dp = 0.0;
		    for (channel=0; channel < audio_file->channels; channel++)
			*dp += SOX_SAMPLE_TO_FLOAT_64BIT(*bp++,clips);
		    *dp++ /= audio_file->channels;
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
		    *sp++ = SOX_SAMPLE_TO_SIGNED(16,*bp++,clips);
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

#if USE_LIBMPG123
    }
#endif

    /* If it stopped before reading all frames, fill the rest with silence */
    if (format == af_double && frames_to_read > 0) {
        memset(write_to, 0, frames_to_read * framesize);
	total_frames += frames_to_read;
	frames_to_read = 0;
    }

    return total_frames;
}

void
close_audio_file(audio_file_t *audio_file)
{
#if USE_LIBAUDIOFILE
    afCloseFile(audio_file->af);
#elif USE_LIBSNDFILE
    sf_close(audio_file->sndfile);
    free(multi_data);
#elif USE_LIBSOX
    sox_close(audio_file->sf);
    free(sox_buf);
#elif LIBAV
    libav_close();
#endif
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
mix_mono_read_doubles(audio_file_t *audio_file, double *data, int frames_to_read)
{
    if (audio_file->channels == 1)
#if USE_LIBAUDIOFILE
	return afReadFrames(audio_file->af, AF_DEFAULT_TRACK, data, frames_to_read);
#elif USE_LIBSNDFILE
	return sf_read_double(audio_file->sndfile, data, frames_to_read);
#endif

    /* Read multi-channel data and mix it down to a single channel of doubles */
    {
	int k, ch, frames_read;
	int dataout = 0;		    /* No of samples written so far */

	if (multi_data_samples < frames_to_read * audio_file->channels) {
	    multi_data = Realloc(multi_data, frames_to_read * audio_file->channels * sizeof(*multi_data));
	    multi_data_samples = frames_to_read * audio_file->channels;
	}

	while (dataout < frames_to_read) {
	    /* Number of frames to read from file */
	    int this_read = frames_to_read - dataout;

#if USE_LIBAUDIOFILE
	    /* A libaudiofile frame is a sample for each channel */
	    frames_read = afReadFrames(audio_file->af, AF_DEFAULT_TRACK, multi_data, this_read);
#elif USE_LIBSNDFILE
	    /* A sf_readf_double frame is a sample for each channel */
	    frames_read = sf_readf_double(audio_file->sndfile, multi_data, this_read);
#endif
	    if (frames_read <= 0)
		break;

	    for (k = 0; k < frames_read; k++) {
		double mix = 0.0;

		for (ch = 0; ch < audio_file->channels; ch++)
		    mix += multi_data[k * audio_file->channels + ch];
		data[dataout + k] = mix / audio_file->channels;
	    }

	    dataout += frames_read;
	}

	return dataout;
  }
} /* mix_mono_read_double */
#endif
