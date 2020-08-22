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
 * Implemented using libsndfile, which can read wav, ogg and flac but not mp3.
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
#include "libsndfile.h"
#include "libmpg123.h"
#include "lock.h"
#include "ui.h"			/* for disp_time, disp_offset and secpp */

#include <string.h>		/* for memset() */

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

    /* These also indicate whether we're using libsndfile or libmpg123 */
    af->sndfile = NULL;
    af->mh = NULL;

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
	if (!libsndfile_open(af, filename)) {
	    free(af);
	    return(NULL);
	}
    }

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
 *
 * The return value is the number of sample frames read,
 * 0 if we are already at end-of-file or
 * a negative value if some kind of read error occurred.
 */

int
read_audio_file(char *data,
		af_format_t format, int channels,
		int start, int frames_to_read)
{
    /* size of one frame of output data in bytes */
    int framesize = (format == af_float ? sizeof(float) : sizeof(short))
    		    * channels;
    int total_frames = 0;	/* How many frames have we filled? */
    char *write_to = data;	/* Where to write next data */

    if (start < 0) {
	/* Is the whole area before 0? */
	if (start + frames_to_read <= 0) {
	    /* All silence */
	    memset(write_to, 0, frames_to_read * framesize);
	    return frames_to_read;
	} else {
	    /* Fill before time 0.0 with silence */
	    int silence = -start;	/* How many silent frames to fill */
	    memset(write_to, 0, silence * framesize);
	    write_to += silence * framesize;
	    total_frames += silence;
	    frames_to_read -= silence;
	    start = 0;	/* Read audio data from start of file */
	}
    }

    if (start >= current_audio_file()->frames) goto fill_with_silence;

    /* Decode MP3's with libmpg123 */
    if (strcasecmp(audio_file->filename + strlen(audio_file->filename) - 4,
    		   ".mp3") == 0) {
	if (libmpg123_seek(audio_file, start) == FALSE) {
	    fprintf(stderr, "Failed to seek in audio file.\n");
	    return -1;
	}
	while (frames_to_read > 0) {
	    int frames = libmpg123_read_frames(audio_file, write_to, frames_to_read, format);
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

	if (!libsndfile_seek(audio_file, start)) {
	    fprintf(stderr, "Failed to seek in audio file.\n");
	    return -1;
	}

	{
	    int frames = libsndfile_read_frames(audio_file, write_to,
	    					frames_to_read, format);
	    if (frames < 0) return -1;

	    total_frames += frames;
	    write_to += frames * framesize;
	    frames_to_read -= frames;
	}
    }

fill_with_silence:
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

    if (af->sndfile) libsndfile_close(af);
    if (af->mh) libmpg123_close(af);

    free(af);
}
