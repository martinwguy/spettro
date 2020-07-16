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

/* libmpg123.c: Interface between spettro and libmpg123 for MP3 decoding
 *
 * Adapted from mpg123's example program feedseek.c, which is copyright 2008
 * by the mpg123 project - free software under the terms of the LGPL 2.1
 */

#include "spettro.h"
#include "libmpg123.h"

#include <mpg123.h>
#include <string.h>	/* for strerror() */
#include <errno.h>

static	long rate;
static	int channels, enc;

/* Open an MP3 file, setting (*afp)->{sample_rate,channels,frames}
 * On failure, returns FALSE.
 */
bool
libmpg123_open(audio_file_t *af, const char *filename)
{
    int ret;
    static bool initialized = FALSE;
 
    if (!initialized) {
	mpg123_init();
	initialized = TRUE;
    }

    if ((af->mh = mpg123_new(NULL, &ret)) == NULL) {
	fprintf(stderr,"Unable to create mpg123 handle: %s\n", mpg123_plain_strerror(ret));
	return FALSE;
    }

    if ((ret = mpg123_param(af->mh, MPG123_FLAGS,
#ifdef MPG123_FORCE_SEEKABLE
	/* Older versions (1.23.8) don't have this */
					     MPG123_FORCE_SEEKABLE |
#endif
    					     MPG123_GAPLESS |
					     MPG123_QUIET, 0)) != MPG123_OK) {
	fprintf(stderr,"Unable to set library options: %s\n", mpg123_plain_strerror(ret));
	return FALSE;
    }

    /* Let the seek index auto-grow and contain an entry for every frame */
    if ((ret = mpg123_param(af->mh, MPG123_INDEX_SIZE, -1, 0)) != MPG123_OK) {
	fprintf(stderr,"Unable to set index size: %s\n", mpg123_plain_strerror(ret));
	return FALSE;
    }

    ret = mpg123_format_none(af->mh);
    if (ret != MPG123_OK) {
	fprintf(stderr,"Unable to disable all output formats: %s\n", mpg123_plain_strerror(ret));
	return FALSE;
    }

    /* Use 16-bit signed output, right for the cache */
    ret = mpg123_format(af->mh, 44100, MPG123_MONO | MPG123_STEREO,  MPG123_ENC_SIGNED_16);
    if (ret != MPG123_OK) {
	fprintf(stderr,"Unable to set float output formats: %s\n", mpg123_plain_strerror(ret));
	return FALSE;
    }

    if ((ret = mpg123_open_feed(af->mh)) != MPG123_OK) {
	fprintf(stderr,"Unable to open feed: %s\n", mpg123_plain_strerror(ret));
	return FALSE;
    }

    if ((af->in = fopen(filename, "rb")) == NULL) {
	fprintf(stderr,"Unable to open input file %s\n", filename);
	return FALSE;
    }

    /* Tell libmpg123 how big the file is */
    if (fseek(af->in, 0, SEEK_END) == 0) {
	mpg123_set_filesize(af->mh, ftell(af->in));
    }
    (void) fseek(af->in, 0, SEEK_SET);

    /* sample rate and channels are set when we read the first frame */
    {
    	short samples[2];	/* Might be stereo */
	if (libmpg123_read_frames(af, (void *)samples, 1, af_signed,
				  &(af->sample_rate),
				  &(af->channels),
				  &(af->frames)) != 1) {
	    fprintf(stderr, "Can't read the first frame.\n");
	    return FALSE;
	}
    }

    return TRUE;
}

/* Seek to the "start"th sample from the MP3 file.
 * Returns TRUE on success, FALSE on failure.
 */
bool
libmpg123_seek(audio_file_t *af, int start)
{
    off_t inoffset;
    int ret;
    int c = '\0';

    /* That condition is tricky... parentheses are crucial... */
    while ((ret = mpg123_feedseek(af->mh, start, SEEK_SET, &inoffset)) == MPG123_NEED_MORE)
    {
	unsigned char uc;
	c = getc(af->in);
	if (c == EOF) break;
	uc = c;

	if (mpg123_feed(af->mh, &uc, 1) == MPG123_ERR) {
	    fprintf(stderr, "Error feeding: %s", mpg123_strerror(af->mh));
	    return FALSE;
	}
    }
    if (c == EOF || ret == MPG123_ERR) {
	fprintf(stderr, "Feedseek failed: %s\n", mpg123_strerror(af->mh));
	return FALSE;
    }

    if (fseek(af->in, inoffset, SEEK_SET) != 0) {
	fprintf(stderr, "fseek failed: %s\n", strerror(errno));
	return FALSE;
    }

    return TRUE;
}

/*
 * Read samples from the MP3 file and convert them to the desired format
 * into the buffer "write_to".
 *
 * The three pointers, if != NULL, are where to store the sample rate,
 * the number of channels and the number of frames, when these change.
 *
 * Returns the number of frames written, or 0 on errors.
 */
int
libmpg123_read_frames(audio_file_t *af, void *write_to, int frames_to_read, af_format_t format,
		      double *sample_rate_p,
		      unsigned *channels_p,
		      unsigned long *frames_p)
{
    /* Where to write to next in the buffer */
    float *fp = (float *) write_to;	/* used when format == af_float */
    char  *bp = (char *)  write_to;	/* used when format == af_signed */
    int frames_written = 0;
    int ret = MPG123_OK;

    while (frames_written < frames_to_read) {
	off_t num;
	unsigned char *audio;
	size_t bytes;
	int c = '\0';

	/*
	 * int mpg123_decode_frame()
	 * Decode next MPEG frame to internal buffer or read a frame and
	 * return after setting a new format.
	 * af->mh		handle
	 * num		current frame offset gets stored there
	 * audio	This pointer is set to the internal buffer
	 *		to read the decoded audio from.
	 * bytes	number of output bytes ready in the buffer 
	 */
	while ((ret = mpg123_decode_frame(af->mh, &num, &audio, &bytes)) == MPG123_NEED_MORE) {
	    unsigned char uc;
	    c = getc(af->in);
	    if (c == EOF) break;
	    uc = c;
	    ret = mpg123_feed(af->mh, &uc, 1);
	    if (ret != MPG123_OK) break;
	}

	/* Check return value from mpg123_decode_frame() */
	
	if (c == EOF || ret == MPG123_ERR) break;

	if (ret == MPG123_NEW_FORMAT) {
	    off_t length;	/* of the MP3 file in samples */

	    mpg123_getformat(af->mh, &rate, &channels, &enc);

	    if (sample_rate_p)	*sample_rate_p = rate;
	    if (channels_p)	*channels_p = channels;

	    length = mpg123_length(af->mh);
	    if (length < 0) {
		fprintf(stderr, "I can't determine the length of the MP3 file.\n");
		if (frames_p) *frames_p = 0;
	    } else {
		/* The docs say mpg123_length() return the number of samples
		 * but it appears to return the number of frames so don't
		 * divide it by the number of channels */
		if (frames_p) *frames_p = length;
	    }

	    switch (format) {
	    case af_float:  af->framesize = sizeof(float);	      break;
	    case af_signed: af->framesize = sizeof(short) * channels; break;
	    default: abort();
	    }
	}

	/* TODO: Cache the whole frame, even if we only return a part of it. */

	/* It returns a whole frame, which may be more than we want */
	if (bytes > (frames_to_read - frames_written) * af->framesize) {
	    bytes = (frames_to_read - frames_written) * af->framesize;
	}

	/* We make libmpg123 return samples as 16-bit mono or stereo,
	 * which is also the format that the audio cache wants. */

	/* Return the required portion of the frame in the required format */
	switch (format) {
	case af_signed:	/* Native-channels 16-bit signed */
	    memcpy(bp, audio, bytes);
	    bp += bytes;
	    frames_written += (bytes/2)/channels;
	    break;
	case af_float:	/* Want mono floats */
	    switch (channels) {
	    case 1:
		{	/* Convert 16-bit signeds to floats */
		    short *isp = (short *)audio;
		    int shorts = bytes / 2;

		    if (bytes % 2 != 0) {
			fprintf(stderr,
				"libmpg123 returns odd byte count\n");
			return 0;
		    }

		    while (shorts > 0) {
			/* Convert [-32767..+32767] to -1..+1 */
			*fp++ = (float)(*isp++) / 32767.0;
			shorts--;
			frames_written++;
		    }
		}
		break;
	    case 2:
		{	/* Convert pairs of 16-bit signeds to floats */
		    short *isp = (short *)audio;
		    int shorts = bytes / 2;

		    if (bytes % 2 != 0) {
			fprintf(stderr,
				"libmpg123 returns odd byte count\n");
			return 0;
		    }
		    if (shorts % 2 != 0) {
			fprintf(stderr,
				"libmpg123 returns odd short count for stereo file.\n");
			return 0;
		    }

		    while (shorts > 0) {
			/* Convert 2 * [-32767..+32767] to -1..+1 */
			*fp++ = ((float)isp[0] + (float)isp[1]) / 65535.0f;
			isp += 2; shorts -= 2;
			frames_written++;
		    }
		    break;
		}
	    }
	}
    }
    if (ret != MPG123_OK) {
	fprintf(stderr, "Error: %s", mpg123_strerror(af->mh));
    }

    return frames_written;
}

void
libmpg123_close(audio_file_t *af)
{
    fclose(af->in);
    mpg123_delete(af->mh);
}
