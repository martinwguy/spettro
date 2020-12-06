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

/* libsndfile.c: Interface between spettro and libsndfile for WAV, OGG, FLAC. */

#include "spettro.h"
#include "libsndfile.h"

#include "lock.h"

#include <string.h>	/* for memset() */

static int mix_mono_read_floats(audio_file_t *af, float *data, int frames_to_read);
/* Buffer used by the above */
static float *multi_data = NULL;   /* buffer for incoming samples */
static int multi_data_samples = 0;  /* length of buffer in samples */

bool
libsndfile_open(audio_file_t *af, char *filename)
{
    SF_INFO info;
    SNDFILE *sndfile;

    memset(&info, 0, sizeof(info));

    if ((sndfile = sf_open(filename, SFM_READ, &info)) == NULL) {
	return FALSE;
    }
    af->filename = filename;
    af->sndfile = sndfile;
    af->sample_rate = info.samplerate;
    af->frames = info.frames;
    af->channels = info.channels;

    return TRUE;
}

/* Seek to the "start"th sample from the MP3 file.
 * Returns TRUE on success, FALSE on failure.
 */
bool
libsndfile_seek(audio_file_t *af, int start)
{
    /* libsndfile doesn't check to see if you're seeking to the same
     * position except for (SEEK_CUR, 0), so check here to avoid
     * restarting a frame-based compressed format decoder.
     + In practice, it is almost always in the right place.
     */
    if (sf_seek(af->sndfile, 0, SEEK_CUR) == start) return TRUE;

    return sf_seek(af->sndfile, start, SEEK_SET) == start;
}

/*
 * Read samples from the audio file and convert them to the desired format
 * into the buffer "write_to".
 *
 * Returns the number of frames written, or a negative value on errors.
 */
int
libsndfile_read_frames(audio_file_t *af,
		      void *write_to,
		      int frames_to_read,
		      af_format_t format)
{
    int total_frames = 0;	/* How many we have writte */

    /* Read from the file until we have read all requested samples */
    while (frames_to_read > 0) {
	int frames;	/* How many frames did the last read() call return? */
	int framesize; /* How many bytes in one sample frame? */

	/* libsndfile's sample frames are a sample for each channel */

	switch (format) {
	case af_float:
	    frames = mix_mono_read_floats(af, (float *)write_to,
	    				  frames_to_read);
	    framesize = sizeof(float);
	    break;
	case af_signed:
	    frames = sf_readf_short(af->sndfile, (short *)write_to, frames_to_read);
	    framesize = sizeof(short) * af->channels;
	    break;
	default:
	    frames = framesize = 0;	/* Avoid compiler warning */
	}

	if (frames > 0) {
	    total_frames += frames;
	    write_to += frames * framesize;
	    frames_to_read -= frames;
	} else if (frames == 0) {
	    /* We ask it to read past EOF so failure is normal */
	    break;
	} else {	/* Probably can't happen */
	    return -1;
	}
    }
    return total_frames;
}

void
libsndfile_close(audio_file_t *af)
{
    sf_close(af->sndfile);
    free(multi_data);
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

static int
mix_mono_read_floats(audio_file_t *af, float *data, int frames_to_read)
{
    if (af->channels == 1)
	return sf_read_float(af->sndfile, data, frames_to_read);

    /* Read multi-channel data and mix it down to a single channel of floats */
    {
	int k, ch, frames_read;
	int dataout = 0;		    /* No of samples written so far */

	lock_buffer();

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

	unlock_buffer();
	return dataout;
  }
}
