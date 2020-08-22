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
	fprintf(stderr,"Unable to create mpg123 handle: %s\n",
		mpg123_plain_strerror(ret));
	return FALSE;
    }

    if (mpg123_open(af->mh, filename) != MPG123_OK) {

	goto fail2;
    }

    {
	long rate; int chans; int encoding;

    	if (mpg123_getformat(af->mh, &rate, &chans, &encoding) != MPG123_OK) {
	    fprintf(stderr, "Can't get MP3 file's format; using 44100Hz.\n");
	    rate = 44100;	/* sure to be playable */
	}
	/* Use 16-bit signed output, right for the cache */
	if (mpg123_format_none(af->mh) != MPG123_OK ||
	    mpg123_format(af->mh, rate,  MPG123_MONO | MPG123_STEREO,
					  MPG123_ENC_SIGNED_16) != MPG123_OK)
	    goto fail;
    }

    {
	long rate; int chans; int encoding;
	off_t length;

	if (mpg123_getformat(af->mh, &rate, &chans, &encoding) != MPG123_OK) {
	    fprintf(stderr, "Can't discover the format of the MP3 file.\n");
	    goto fail;
	}
	af->channels = chans;
	af->sample_rate = rate;

	(void) mpg123_scan(af->mh);
	length = mpg123_length(af->mh);
	if (length < 0) {
	    fprintf(stderr, "Can't discover the length of the MP3 file.\n");
	    goto fail;
	}
	af->frames = length;
    }

    return TRUE;

fail:
    mpg123_close(af->mh);
fail2:
    mpg123_delete(af->mh);
    af->mh = NULL;
    return FALSE;
}

/* Seek to the "start"th sample from the MP3 file.
 * Returns TRUE on success, FALSE on failure.
 */
bool
libmpg123_seek(audio_file_t *af, int start)
{
    if (mpg123_seek(af->mh, (off_t)start, SEEK_SET) == MPG123_ERR) {
	fprintf(stderr, "Failed to seek in MP3 file: %s\n", mpg123_strerror(af->mh));
	return FALSE;
    }

    return TRUE;
}

/*
 * Read samples from the MP3 file and convert them to the desired format
 * into the buffer "write_to".
 *
 * Returns the number of frames written, or a negative value on errors.
 */
int
libmpg123_read_frames(audio_file_t *af,
		      void *write_to,
		      int frames_to_read,
		      af_format_t format)
{
    int framesize = 0;		/* Size of sample frames */
    size_t bytes_written;

    switch (format) {
    case af_float:  fprintf(stderr, "Can't read float frames from MP3 file.\n");
    		    return -1;
    case af_signed: framesize = sizeof(short) * af->channels; break;
    default: abort();
    }

    /* Avoid reading past end of file because that makes the whole read fail */
    {
    	off_t r = mpg123_tell(af->mh);
	if (r < 0) {
	    fprintf(stderr, "libmpg123 can't tell where it is!\n");
	    return -1;
	}

	/* mpg123_tell() returns result in sample frames,
	 * not in samples like it says in the docs */
	if (r + frames_to_read > af->frames) {
	    frames_to_read = af->frames - r;
	}
    }

    if (mpg123_read(af->mh, write_to, frames_to_read * framesize,
    		    &bytes_written) != MPG123_OK) {
	return -1;
    }

    return bytes_written / framesize;
}

void
libmpg123_close(audio_file_t *af)
{
    mpg123_close(af->mh);
    mpg123_delete(af->mh);
}
