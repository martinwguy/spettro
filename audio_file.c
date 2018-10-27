/*
 * audio_file.c - Stuff to read audio samples from a sound file
 *
 * Implemented using one of:
 * - libaudiofile, which can only read a few formats, and not ogg or mp3,
 * - linsndfile, which can read ogg but not mp3.
 *
 * Identifier names containing "audiofile" are libaudiofile library functions,
 * and those containing "audio_file" refer to this lib-independent layer.
 */

#include "spettro.h"
#include "audio_file.h"		/* Our header file */

#include "lock.h"

#include <string.h>		/* for memset() */

static int mix_mono_read_doubles(audio_file_t *af, double *data, int frames_to_read);

/* Audio file info */
double		audio_length = 0.0;	/* Length of the audio in seconds */
double		sample_rate;		/* SR of the audio in Hertz */

audio_file_t *
open_audio_file(char *filename)
{
#if USE_LIBAUDIOFILE

    AFfilehandle af;
    audio_file_t *audio_file = malloc(sizeof(audio_file_t));

    if (audio_file == NULL) {
	fprintf(stderr, "Out of memory in open_audio_file()\n");
	return(NULL);
    }

    if ((af = afOpenFile(filename, "r", NULL)) == NULL) {
	/* By default it prints a line to stderr, which is what we want.
	 * For better error handling use afSetErrorHandler() */
	free(audio_file);
	return(NULL);
    }
    audio_file->af = af;
    audio_file->samplerate = afGetRate(af, AF_DEFAULT_TRACK);
    audio_file->frames = afGetFrameCount(af, AF_DEFAULT_TRACK);
    audio_file->channels = afGetChannels(af, AF_DEFAULT_TRACK);

#elif USE_LIBSNDFILE

    static audio_file_t our_audio_file;
    audio_file_t *audio_file = &our_audio_file;
    SF_INFO info;
    SNDFILE *sndfile;

    memset(&info, 0, sizeof(info));

    if ((sndfile = sf_open(filename, SFM_READ, &info)) == NULL) {
	fprintf(stderr, "libsndfile failed to open \"%s\": %s\n",
		filename, sf_strerror(NULL));
	return(NULL);
    }
    audio_file->sndfile = sndfile;
    audio_file->samplerate = info.samplerate;
    audio_file->frames = info.frames;
    audio_file->channels = info.channels;

#endif

    /* Set the globals that everyone picks at */
    sample_rate = audio_file->samplerate;
    audio_length = (double)audio_file->frames / sample_rate;

    return(audio_file);
}

/*
 * Read sample frames, returning them as mono doubles for the graphics or as
 * the original audio as 16-bit in system-native bytendianness for the sound.
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
int
read_audio_file(audio_file_t *audio_file, char *data,
		af_format format, int channels,
		int start, int frames_to_read)
{
#if USE_LIBAUDIOFILE
    AFfilehandle af = audio_file->af;
#elif USE_LIBSNDFILE
    SNDFILE *sndfile = audio_file->sndfile;
#endif
    /* size of one frame of output data in bytes */
    int framesize = (format == af_double ? sizeof(double) : sizeof(short))
		    * channels;
    int total_frames = 0;	/* How many frames have we filled? */
    char *write_to = data;	/* Where to write next data */
    int frames;		/* How many frames did the last read() call return? */

    if (!lock_audio_file()) {
	fprintf(stderr, "Cannot lock audio file\n");
	exit(1);
    }

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
	total_frames += silence;
        write_to += silence * framesize;
        frames_to_read -= silence;
	start = 0;	/* Read audio data from start of file */
    }

    if (
#if USE_LIBAUDIOFILE
        afSeekFrame(af, AF_DEFAULT_TRACK, start)
#elif USE_LIBSNDFILE
        sf_seek(sndfile, start, SEEK_SET)
#endif
	!= start) {
	fprintf(stderr, "Failed to seek in audio file.\n");
	return 0;
    }

    do {
	/* libaudio and libsndfile's sample frames are a sample for each channel */
#if USE_LIBAUDIOFILE
	/* libaudiofile does the mixing down to one channel for doubles */
        frames = afReadFrames(af, AF_DEFAULT_TRACK, write_to, frames_to_read);
#elif USE_LIBSNDFILE
	if (format == af_double) {
            frames = mix_mono_read_doubles(audio_file, (double *)write_to, frames_to_read);
	} else {
	    if (channels != audio_file->channels) {
		fprintf(stderr, "Wrong number of channels in signed audio read!\n");
		return 0;
	    }
	    frames = sf_readf_short(sndfile, (short *)write_to, frames_to_read);
	}
#endif
        if (frames > 0) {
	    total_frames += frames;
            write_to += frames * framesize;
            frames_to_read -= frames;
        } else {
            /* We ask it to read past EOF so failure is normal */
        }
    /* while we still need to read stuff and the last read didn't fail */
    } while (frames_to_read > 0 && frames > 0);

    if (!unlock_audio_file()) {
	fprintf(stderr, "Cannot unlock audio file\n");
	exit(1);
    }

    /* If it stopped before reading all frames, fill the rest with silence */
    if (format == af_double && frames_to_read > 0) {
        memset(data + (total_frames * framesize), 0, frames_to_read * framesize);
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
#endif
}

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
	static double *multi_data = NULL;   /* buffer for incomig samples */
	static int multi_data_samples = 0;  /* length of buffer in samples */
	int k, ch, frames_read;
	int dataout = 0;		    /* No of samples written so far */

	if (multi_data_samples < frames_to_read * audio_file->channels) {
	    multi_data = realloc(multi_data, frames_to_read * audio_file->channels * sizeof(*multi_data));
	    if (multi_data == NULL) {
		fprintf(stderr, "Out of memory in mix_mono_doubles\n");
		exit(1);
	    }
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
