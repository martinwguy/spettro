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

#include <stdlib.h>		/* for malloc() */
#include <stdio.h>		/* for error messages */
#include <string.h>		/* for memset() */

#if USE_LIBSNDFILE
static sf_count_t sfx_mix_mono_read_doubles(SNDFILE *file, double *data,
					    sf_count_t datalen);
#endif

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

    /*
     * We will be reading as mono doubles or as 16-bit native for soundcard,
     * both native-endian. Don't care if it fails.
     */
    (void) afSetVirtualByteOrder(af, AF_DEFAULT_TRACK,
#if LITTLE_ENDIAN
			      AF_BYTEORDER_LITTLEENDIAN);
#else
			      AF_BYTEORDER_BIGENDIAN);
#endif

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
 * "start" is the index of the first sample frame to read and may be negative.
 * "nframes" is the number of sample frames to read.
 */
int
read_audio_file(audio_file_t *audio_file, char *data,
		af_format format, int channels,
		int start, int nframes)
{
#if USE_LIBAUDIOFILE
    AFfilehandle af = audio_file->af;
#elif USE_LIBSNDFILE
    SNDFILE *sndfile = audio_file->sndfile;
#endif
    int frames;		/* How many did the last read() call return? */
    int total_frames = 0;	/* How many frames have we read? */
    int framesize = (format == af_double ? sizeof(double) : sizeof(short))
		    * channels;

    if (!lock_audiofile()) {
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

    if (start >= 0) {
#if USE_LIBAUDIOFILE
        afSeekFrame(af, AF_DEFAULT_TRACK, start);
#elif USE_LIBSNDFILE
        sf_seek(sndfile, start, SEEK_SET);
#endif
    } else {
	/* Fill before time 0.0 with silence */
        start = -start;
#if USE_LIBAUDIOFILE
        afSeekFrame(af, AF_DEFAULT_TRACK, 0);
#elif USE_LIBSNDFILE
        sf_seek(sndfile, 0, SEEK_SET);
#endif
        memset(data, 0, start * framesize);
        data += start * framesize;
        nframes -= start;
    }
    do {
#if USE_LIBAUDIOFILE
        frames = afReadFrames(af, AF_DEFAULT_TRACK, (void *)data, nframes);
#elif USE_LIBSNDFILE
	if (format == af_double) {
            frames = sfx_mix_mono_read_doubles(sndfile, (double *)data, nframes);
	} else {
	    /* 16-bit native endian */
	    frames = sf_readf_short(sndfile, (short *)data, nframes);
	}
#endif
        if (frames > 0) {
	    total_frames += frames;
            data += frames * framesize;
            nframes -= frames;
        } else {
            /* We ask it to read past EOF so failure is normal */
        }
    /* while we still need to read stuff and the last read didn't fail */
    } while (nframes > 0 && frames > 0);

    if (!unlock_audiofile()) {
	fprintf(stderr, "Cannot unlock audio file\n");
	exit(1);
    }

    /* If it stopped before reading all frames, fill the rest with silence */
    if (nframes > 0) {
        memset(data + (total_frames * framesize), 0, nframes * framesize);
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

#endif /* USE_LIBSNDFILE */
