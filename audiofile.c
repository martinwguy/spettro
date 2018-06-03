/*
 * audiofile.c - Stuff to read audio samples from a sound file
 *
 * Currently implemented using libaudiofile, which cannot read Ogg or MP3.
 */

#include <stdlib.h>		/* for malloc() */
#include <audiofile.h>		/* libaudiofile's header */
#include <stdio.h>		/* for error messages */
#include <string.h>		/* for memset() */

#include "audiofile.h"		/* Our header file */

audio_file_t *
open_audio_file(char *filename)
{
    AFfilehandle af;
    audio_file_t *audio_file = malloc(sizeof(audio_file_t));

    if (audio_file == NULL) {
	fprintf(stderr, "Out of memory in open_audio_file()\n");
	return(NULL);
    }

    if ((af = afOpenFile(filename, "r", NULL)) == NULL) {
	fprintf(stderr, "libaudiofile failed to open the file.\n");
	free(audio_file);
	return(NULL);
    }
    audio_file->af = af;

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

    return(audio_file);
}

int
audio_file_length_in_frames(audio_file_t *audio_file)
{
    return(afGetFrameCount(audio_file->af, AF_DEFAULT_TRACK));
}

/* Number of audio channels */
int
audio_file_channels(audio_file_t *audio_file)
{
    return(afGetChannels(audio_file->af, AF_DEFAULT_TRACK));
}

double
audio_file_sampling_rate(audio_file_t *audio_file)
{
    return(afGetRate(audio_file->af, AF_DEFAULT_TRACK));
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
    AFfilehandle af = audio_file->af;
    int frames;		/* How many did the last read() call return? */
    int total_frames = 0;
    int framesize = (format == af_double ? sizeof(double) : sizeof(short))
		    * channels;

    if (afSetVirtualSampleFormat(af, AF_DEFAULT_TRACK,
	format == af_double ? AF_SAMPFMT_DOUBLE : AF_SAMPFMT_TWOSCOMP,
	format == af_double ? sizeof(double) : sizeof(short)) ||
        afSetVirtualChannels(af, AF_DEFAULT_TRACK, channels)) {
            fprintf(stderr, "Can't set virtual sample format.\n");
	    return 0;
    }

    if (start >= 0) {
        afSeekFrame(af, AF_DEFAULT_TRACK, start);
    } else {
	/* Fill before time 0.0 with silence */
        start = -start;
        afSeekFrame(af, AF_DEFAULT_TRACK, 0);
        memset(data, 0, start * framesize);
        data += start * framesize;
        nframes -= start;
    }
    do {
        frames = afReadFrames(af, AF_DEFAULT_TRACK, (void *)data, nframes);
        if (frames > 0) {
	    total_frames += frames;
            data += frames;
            nframes -= frames;
        } else {
            /* We ask it to read past EOF so failure is normal */
        }
    /* while we still need to read stuff and the last read didn't fail */
    } while (nframes > 0 && frames > 0);

    if (total_frames < nframes) {
        memset(data, 0, (nframes - total_frames) * sizeof(data[0]));
    }

    return total_frames;
}

void
close_audio_file(audio_file_t *audio_file)
{
    afCloseFile(audio_file->af);
}
