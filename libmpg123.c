/* libmpg123.c: Interface between spettro and libmpg123 for MP3 decoding
 *
 * Adapted from mpg123's example program feedseek.c, which is copyright 2008
 * by the mpg123 project - free software under the terms of the LGPL 2.1
 */

#if USE_LIBMPG123

#include "spettro.h"
#include "libmpg123.h"

#include <mpg123.h>
#include <string.h>	/* for strerror() */
#include <errno.h>

static	long rate;
static	int channels, enc;

static	unsigned char *audio;
static	FILE *in;
static	mpg123_handle *m;
static	int ret;
static	off_t num;
static	size_t bytes;

/* Open an MP3 file.
 * On failure, sets the supplied audio_file_t * to NULL.
 */
void
libmpg123_open(audio_file_t **afp, const char *filename)
{
    mpg123_init();

    if ((m = mpg123_new(NULL, &ret)) == NULL) {
	fprintf(stderr,"Unable to create mpg123 handle: %s\n", mpg123_plain_strerror(ret));
	goto fail;
    }

    if ((ret = mpg123_param(m, MPG123_FLAGS,
#ifdef MPG123_FORCE_SEEKABLE
	/* Older versions (1.23.8) don't have this */
					     MPG123_FORCE_SEEKABLE |
#endif
    					     MPG123_GAPLESS |
					     MPG123_QUIET, 0)) != MPG123_OK) {
	fprintf(stderr,"Unable to set library options: %s\n", mpg123_plain_strerror(ret));
	goto fail;
    }

    /* Let the seek index auto-grow and contain an entry for every frame */
    if ((ret = mpg123_param(m, MPG123_INDEX_SIZE, -1, 0)) != MPG123_OK) {
	fprintf(stderr,"Unable to set index size: %s\n", mpg123_plain_strerror(ret));
	goto fail;
    }

    ret = mpg123_format_none(m);
    if (ret != MPG123_OK) {
	fprintf(stderr,"Unable to disable all output formats: %s\n", mpg123_plain_strerror(ret));
	goto fail;
    }

    /* Use 16-bit signed output, right for the cache */
    ret = mpg123_format(m, 44100, MPG123_MONO | MPG123_STEREO,  MPG123_ENC_SIGNED_16);
    if (ret != MPG123_OK) {
	fprintf(stderr,"Unable to set float output formats: %s\n", mpg123_plain_strerror(ret));
	goto fail;
    }

    if ((ret = mpg123_open_feed(m)) != MPG123_OK) {
	fprintf(stderr,"Unable to open feed: %s\n", mpg123_plain_strerror(ret));
	goto fail;
    }

    if ((in = fopen(filename, "rb")) == NULL) {
	fprintf(stderr,"Unable to open input file %s\n", filename);
	goto fail;
    }

    /* Tell libmpg123 how big the file is */
    if (fseek(in, 0, SEEK_END) == 0) {
	mpg123_set_filesize(m, ftell(in));
    }
    (void) fseek(in, 0, SEEK_SET);

    /* sample rate and channels are set when we read the first frame */
    {
    	short samples[2];	/* Might be stereo */
	if (libmpg123_read_frames((void *)samples, 1, af_signed) != 1) {
	    fprintf(stderr, "Can't read the first frame.\n");
	    goto fail;
	}
    }

    return;

fail:
    *afp = NULL;
}

/* Seek to the "start"th sample from the MP3 file.
 * Returns TRUE on success, FALSE on failure.
 */
bool
libmpg123_seek(int start)
{
    off_t inoffset;

    /* That condition is tricky... parentheses are crucial... */
    while ((ret = mpg123_feedseek(m, start, SEEK_SET, &inoffset)) == MPG123_NEED_MORE)
    {
	int c = getc(in);
	unsigned char uc;
	if (c == EOF) break;
	uc = c;

	if (mpg123_feed(m, &uc, 1) == MPG123_ERR) {
	    fprintf(stderr, "Error feeding: %s", mpg123_strerror(m));
	    return FALSE;
	}
    }
    if (ret == MPG123_ERR) {
	fprintf(stderr, "Feedseek failed: %s\n", mpg123_strerror(m));
	return FALSE;
    }

    if (fseek(in, inoffset, SEEK_SET) != 0) {
	fprintf(stderr, "fseek failed: %s\n", strerror(errno));
	return FALSE;
    }

    return TRUE;
}

/*
 * Read samples from the MP3 file and convert them to the desired format
 * into the buffer "write_to".
 *
 * Returns the number of frames written, or 0 on errors.
 */
int
libmpg123_read_frames(void *write_to, int frames_to_read, af_format_t format)
{
    /* Where to write to next in the buffer */
    double *dp = (double *) write_to;	/* used when format == af_double */
    char   *bp = (char *)   write_to;	/* used when format == af_signed */
    int frames_written = 0;
    static int framesize;

    while (frames_written < frames_to_read) {
	while ((ret = mpg123_decode_frame(m, &num, &audio, &bytes)) == MPG123_NEED_MORE) {
	    int c = getc(in);
	    unsigned char uc;
	    if (c == EOF) break;
	    uc = c;
	    ret = mpg123_feed(m, &uc, 1);
	    if (ret != MPG123_OK) break;
	}

	/* Check return value from mpg123_decode_frame() */
	
	if (ret == MPG123_ERR) break;

	if (ret == MPG123_NEW_FORMAT) {
	    off_t length;	/* of the MP3 file in samples */

	    mpg123_getformat(m, &rate, &channels, &enc);
	    audio_file->sample_rate = rate;
	    audio_file->channels = channels;
	    length = mpg123_length(m);
	    if (length < 0) {
		fprintf(stderr, "I can't determine the length of the MP3 file.\n");
		audio_file->frames = 0;
	    } else {
		/* The docs say mpg123_length() return the number of samples
		 * but it appears to return the number of frames so don't
		 * divide it by the number of channels */
		audio_file->frames = length;
	    }

	    switch (format) {
	    case af_double: framesize = sizeof(double);	      break;
	    case af_signed: framesize = sizeof(short) * channels; break;
	    default: abort();
	    }
	}

	/* It returns a whole frame, which may be more than we want */
	if (bytes > (frames_to_read - frames_written) * framesize) {
	    bytes = (frames_to_read - frames_written) * framesize;
	}

	/* We make libmpg123 return samples as 16-bit mono or stereo */
	switch (format) {
	case af_signed:	/* Native-channels 16-bit signed */
	    memcpy(bp, audio, bytes);
	    bp += bytes;
	    frames_written += (bytes/2)/channels;
	    break;
	case af_double:	/* Want mono doubles */
	    switch (channels) {
	    case 1:
		{	/* Convert 16-bit signeds to doubles */
		    short *isp = (short *)audio;
		    int shorts = bytes / 2;

		    if (bytes % 2 != 0) {
			fprintf(stderr,
				"libmad123 returns odd byte count\n");
			return 0;
		    }

		    while (shorts > 0) {
			/* Convert [-32767..+32767] to -1..+1 */
			*dp++ = (double)(*isp++) / 32767.0;
			shorts--;
			frames_written++;
		    }
		}
		break;
	    case 2:
		{	/* Convert pairs of 16-bit signeds to doubles */
		    short *isp = (short *)audio;
		    int shorts = bytes / 2;

		    if (bytes % 2 != 0) {
			fprintf(stderr,
				"libmad123 returns odd byte count\n");
			return 0;
		    }
		    if (shorts % 2 != 0) {
			fprintf(stderr,
				"libmad123 returns odd short count for stereo file.\n");
			return 0;
		    }

		    while (shorts > 0) {
			/* Convert 2 * [-32767..+32767] to -1..+1 */
			*dp++ = ((double)isp[0] + (double)isp[1]) / 65534.0;
			isp += 2; shorts -= 2;
			frames_written++;
		    }
		    break;
		}
	    }
	}
    }
    if (ret != MPG123_OK) {
	fprintf(stderr, "Error: %s", mpg123_strerror(m));
    }

    return frames_written;
}

void
libmpg123_close()
{
    fclose(in);
    mpg123_delete(m);
    mpg123_exit();
}

#endif
