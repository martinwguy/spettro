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
 * Implemented using libsndfile, which can read ogg but not mp3.
 * and libmpg123, because spettro needs sample-accurate seeking.
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

#include "convert.h"
#include "libmpg123.h"
#include "lock.h"
#include "ui.h"			/* for disp_time, disp_offset and secpp */

#include <string.h>		/* for memset() */

/* Stuff for libsndfile */
static int mix_mono_read_floats(audio_file_t *af, float *data, int frames_to_read);
/* Buffer used by the above */
static float *multi_data = NULL;   /* buffer for incoming samples */
static int multi_data_samples = 0;  /* length of buffer in samples */

#include "libmpg123.h"

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
 * so we use libsndfile for that.
 */
audio_file_t *
open_audio_file(char *filename)
{
    audio_file_t *af = Malloc(sizeof(*af));
    SF_INFO info;
    SNDFILE *sndfile;

    af->audio_buf = NULL;
    af->audio_buflen = 0;

    /* Decode MP3's with libmpg123 */
    if (strcasecmp(filename + strlen(filename)-4, ".mp3") == 0) {
	if (!libmpg123_open(af, filename)) {
	    free(af);
	    return NULL;
	}
    } else {
	/* for anything else, use libsndfile */

	memset(&info, 0, sizeof(info));

	if ((sndfile = sf_open(filename, SFM_READ, &info)) == NULL) {
	    free(af);
	    return NULL;
	}
	af->sndfile = sndfile;
	af->sample_rate = info.samplerate;
	af->frames = info.frames;
	af->channels = info.channels;
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

/* What is the sample rate of the audio file? */
double
current_sample_rate()
{
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

int
read_audio_file(char *data,
		af_format_t format, int channels,
		int start, int frames_to_read)
{
    SNDFILE *sndfile = audio_file->sndfile;

    /* size of one frame of output data in bytes */
    int framesize = (format == af_float ? sizeof(float) : sizeof(short))
    		    * channels;
    int total_frames = 0;	/* How many frames have we filled? */
    char *write_to = data;	/* Where to write next data */

    if (start < 0) {
	/* Fill before time 0.0 with silence */
        int silence = -start;	/* How many silent frames to fill */
        memset(write_to, 0, silence * framesize);
        write_to += silence * framesize;
	total_frames += silence;
        frames_to_read -= silence;
	start = 0;	/* Read audio data from start of file */
    }

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
    } else {
	/* and anything else with libsndfile */

	if (sf_seek(sndfile, start, SEEK_SET) != start) {
	    fprintf(stderr, "Failed to seek in audio file.\n");
	    return 0;
	}

	/* Read from the file until we have read all requested samples */
	while (frames_to_read > 0) {
	    int frames;	/* How many frames did the last read() call return? */

	    /* libsndfile's sample frames are a sample for each channel */

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
    if (frames_to_read > 0) {
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

    sf_close(af->sndfile);
    free(multi_data);
    free(af);
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
mix_mono_read_floats(audio_file_t *af, float *data, int frames_to_read)
{
    if (af->channels == 1)
	return sf_read_float(af->sndfile, data, frames_to_read);

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

	    /* A sf_readf_float frame is a sample for each channel */
	    frames_read = sf_readf_float(af->sndfile, multi_data, this_read);

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
