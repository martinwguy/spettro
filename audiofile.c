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

    /* We will only be reading as mono doubles and
     * libaudiofile with virtual nchannels of 1 averages mixes stereo to mono.
     */
    if (afSetVirtualSampleFormat(af, AF_DEFAULT_TRACK, AF_SAMPFMT_DOUBLE,
                                                       sizeof(double)) ||
        afSetVirtualChannels(af, AF_DEFAULT_TRACK, 1)) {
            fprintf(stderr, "Can't set virtual sample format.\n");
	free(audio_file);
            return NULL;
    }

    return(audio_file);
}

int
audio_file_length_in_frames(audio_file_t *audio_file)
{
    return(afGetFrameCount(audio_file->af, AF_DEFAULT_TRACK));
}

double
audio_file_sampling_rate(audio_file_t *audio_file)
{
    return(afGetRate(audio_file->af, AF_DEFAULT_TRACK));
}

/*
 * Read sample frames, returning them as mono doubles.
 *
 * "start" is the index of the first sample frame to read and may be negative.
 * "nframes" is the number of sample frames to read.
 * "data" is where to put the monoised data.
 */
int
read_mono_audio_double(audio_file_t *audio_file, double *data,
		       int start, int nframes)
{
    AFfilehandle af = audio_file->af;
    int frames;		/* How many did the last read() call return? */
    int total_frames = 0;

    if (start >= 0) {
        afSeekFrame(af, AF_DEFAULT_TRACK, start);
    } else {
	/* Fill before time 0.0 with silence */
        start = -start;
        afSeekFrame(af, AF_DEFAULT_TRACK, 0);
        memset(data, 0, start * sizeof(data[0]));
        data += start;
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

    return(total_frames);
}

void
close_audio_file(audio_file_t *audio_file)
{
    afCloseFile(audio_file->af);
}
