/*
 * libsndfile.c - Stuff to read audio samples from a sound file
 *
 * This implementation uses libsndfile, which can read Ogg but not MP3.
 */

#include <stdlib.h>		/* for malloc() */
#include <stdio.h>		/* for error messages */
#include <string.h>		/* for memset() */
#include <sndfile.h>		/* libsndfile's header */

#include "sndfile.h"		/* Our header file */

static sf_count_t sfx_mix_mono_read_doubles(SNDFILE *file, double *data,
					    sf_count_t datalen);

audio_file_t *
open_audio_file(char *filename)
{
    static audio_file_t audio_file;
    SF_INFO info;
    SNDFILE *sndfile;

    memset(&info, 0, sizeof(info));

    if ((sndfile = sf_open(filename, SFM_READ, &info)) == NULL) {
	fprintf(stderr, "libsndfile failed to open the file.\n");
	return(NULL);
    }
    audio_file.sndfile = sndfile;
    audio_file.samplerate = info.samplerate;
    audio_file.frames = info.frames;
    audio_file.channels = info.channels;

    return(&audio_file);
}

int
audio_file_length_in_frames(audio_file_t *audio_file)
{
    return audio_file->frames;
}

int
audio_file_channels(audio_file_t *audio_file)
{
    return(audio_file->channels);
}

double
audio_file_sampling_rate(audio_file_t *audio_file)
{
    return audio_file->samplerate;
}

/*
 * Read sample frames, returning them as mono doubles for the graphics or as
 * the original audio as 16-bit in system-native bytendianness for the sound.
 *
 * "data" is where to put the audio data.
 * "format" is one of af_double or af_signed
 * "channels" is the number of desired channels, 1 to monoise or copied from
 *		the WAV file to play as-is.
 * "start" is the index of the first sample frame to read and may be negative.
 * "nframes" is the number of sample frames to read.
 */
int
read_audio_file(audio_file_t *audio_file, char *data,
		af_format format, int channels,
		int start, int nframes)
{
    SNDFILE *sndfile = audio_file->sndfile;
    int frames;		/* How many did the last read() call return? */
    int total_frames = 0;
    int framesize = (format == af_double ? sizeof(double) : sizeof(short))
		    * channels;

    if (start >= 0) {
        sf_seek(sndfile, start, SEEK_SET);
    } else {
	/* Fill before time 0.0 with silence */
        start = -start;
        sf_seek(sndfile, 0, SEEK_SET);
        memset(data, 0, start * framesize);
        data += start * framesize;
        nframes -= start;
    }
    
    do {
	if (format == af_double) {
            frames = sfx_mix_mono_read_doubles(sndfile, (double *)data, nframes);
	} else {
	    /* 16-bit native endian */
	    frames = sf_readf_short(sndfile, (short *)data, nframes);
	}
        if (frames > 0) {
	    total_frames += frames;
            data += frames * framesize;
            nframes -= frames;
        } else {
            /* We ask it to read past EOF so failure is normal */
        }
    /* while we still need to read stuff and the last read didn't fail */
    } while (nframes > 0 && frames > 0);

    if (total_frames < nframes) {
        memset(data, 0, (nframes - total_frames) * framesize);
    }

    return(total_frames);
}

void
close_audio_file(audio_file_t *audio_file)
{
    sf_close(audio_file->sndfile);
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

static sf_count_t
sfx_mix_mono_read_doubles (SNDFILE * file, double * data, sf_count_t datalen)
{
	SF_INFO info ;

	sf_command (file, SFC_GET_CURRENT_SF_INFO, &info, sizeof (info)) ;

	if (info.channels == 1)
		return sf_read_double (file, data, datalen) ;
      {
	static double multi_data [2048] ;
	int k, ch, frames_read ;
	sf_count_t dataout = 0 ;

	while (dataout < datalen)
	{	int this_read ;

		this_read = MIN (ARRAY_LEN (multi_data) / info.channels, datalen - dataout) ;

		frames_read = sf_readf_double (file, multi_data, this_read) ;
		if (frames_read == 0)
			break ;

		for (k = 0 ; k < frames_read ; k++)
		{	double mix = 0.0 ;

			for (ch = 0 ; ch < info.channels ; ch++)
				mix += multi_data [k * info.channels + ch] ;
			data [dataout + k] = mix / info.channels ;
			} ;

		dataout += frames_read ;
		} ;

	return dataout ;
      }
} /* sfx_mix_mono_read_double */
