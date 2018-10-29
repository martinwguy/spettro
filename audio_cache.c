/* audio_cache.c: like read_audio_file but caching the audio data */

/* For compressed audio files (MPG, OGG, M4A, FLAC) we cache the decoded audio.
 *
 * We keep the decoded audio data as 16-bit native-endian shorts with the
 * same number of channels as the audio file, with a funny encoding.
 * Values 1 to 65535 encode -1.0 to 1.0 (or -32767 to +32767) while
 * 0 means "this sample hasn't been decoded yet".
 * This is because reads from a sparse file return 0 bytes, so if they
 * seek far forwards and then go back, we can see which samples are cached
 * and which not.
 * Stdio, unfortunately, doesn't seem able to create sparse files, as you
 * can't fseek past the end of the file.
 *
 * We keep a file descriptor, "cache", open to the teporary file and delete it
 * so no cleanup is necessary on exit (on Unix and Unix-alikes!).
 * If cache < 0 that means that no cache is in operation, which is used
 * for uncompressed audio files.
 */

#include "spettro.h"
#include "audio_cache.h"

#include "audio_file.h"

#include <unistd.h>	/* for Unix file system calls */
#include <string.h>	/* for memset() */
#include <assert.h>	/* for memset() */

/* Functions to convert between the cache format and 16-bit signed */
static void convert_signed_to_cached(short *to, short *from, long len);
static void convert_cached_to_signed(short *to, short *from, long len);
static void convert_cached_to_mono_double(double *to, short *from, long len, int channels);

/* Read uncached data from the audio file into a hole in our data */
static void fill_hole(
	audio_file_t *audio_file,
	short *hole_start,	/* Where to write newly-cached data into */
	long hole_size,		/* How many samples to fill in */
	long start);		/* Frame offset in audio file to read from */

static int cache = -1;

/* Create the cache file and return a pointer to its FILE pointer or */
void
create_audio_cache()
{
    char tmpfilename[] = "/tmp/spettro-XXXXXX";
    cache = mkstemp(tmpfilename);

    if (cache < 0) {
    	fprintf(stderr, "Cannot create decompressed audio cache file.\n");
	return;
    }

    if (remove(tmpfilename) != 0) {;
    	fprintf(stderr, "Warning: Cannot remove decompressed audio cache file %s.\n", tmpfilename);
	/* Harmless */
    }
}

void
no_audio_cache()
{
    if (cache >= 0) close(cache);
    cache = -1;
}

/* Read audio data using our cache. Same interface as read_audio_file().
 *
 * Note: "channels" is the number of channels they want returned;
 * audio_file->channels is the number of channels in the original file
 */
int
read_cached_audio(audio_file_t *audio_file, char *data,
		  af_format format, int channels,
		  int start, int frames_to_read)
{
    static short *buf = NULL;	/* The required audio data as 16-bit signed,
    				 * with same number of channels as audio file */
    static int buflen = 0;	/* Memory allocated to buf[], in samples */
    static int samples_in_buf = 0;/* Number of samples read into buf[] */
    long offset;		/* temp: Where to seek to in the cache file */

    short *hole_start = NULL;	/* No hole found yet */
    long hole_size;		/* Size of the hole we found, in samples */
    long total_frames = 0;	/* How many sample frames have we filled? */

    int i;
    short *sp;

    if (cache < 0) {
	return read_audio_file(audio_file, data, format, channels,
				  start, frames_to_read);
    }

    /* Fill space before the start with silence. Only happens with af_double
     * and always includes part of the actual data */
    if (start < 0) {
	int silence = -start;
        assert (format == af_double);
	memset(data, 0, silence * sizeof(double));
	data += silence * sizeof(double);
	total_frames += silence;
	frames_to_read -= silence;
	start = 0;
    }

    /* Make sure our buffer is big enough */
    if (buflen < frames_to_read * audio_file->channels) {
    	free(buf);
	buflen = frames_to_read * audio_file->channels;
    	buf = malloc(buflen * sizeof(*buf));
	if (buf == NULL) {
	    fprintf(stderr, "Out of memory in do_read_audio_file\n");
	    exit(1);
	}
    }
    /* Set all to 0 so that reads past the end of the cache file leave the
     * data as "not cached yet" */
    memset(buf, 0, buflen * sizeof(*buf));

    /* Try to fetch cached data from the file. As it's read-write, a seek
     * past the end of the file should extend it silently. */
    offset = (long)start * audio_file->channels * sizeof(short);
    long new_offset;
    if ((new_offset = lseek(cache, offset, SEEK_SET)) != offset) {
        fprintf(stderr, "Can't seek to offset %ld in cache file: lseek returns %ld\n",
		new_offset, offset);
	perror("lseek");	
	return(0);
    } else {
        int bytes_read = read(cache, (char *)buf,
	     frames_to_read * audio_file->channels * sizeof(*buf));
	samples_in_buf = bytes_read / sizeof(short);
	int frames_read = samples_in_buf / audio_file->channels;
        /* If the file read fails, is at-or-after EOF or is short, that leaves
	 * zeroes in the buffer to be filled in from the real file.
	 */
	if (frames_read - frames_to_read) {
	    fill_hole(audio_file, buf + samples_in_buf,
		      (frames_to_read - frames_read) * audio_file->channels,
		      start + frames_read);
	    samples_in_buf += (frames_to_read - frames_read) * audio_file->channels;
	}
    }

    assert(samples_in_buf == frames_to_read * audio_file->channels);

    /* Now check for zones of uncached samples in the cached data
     * and replace them with the real data */
    hole_start = NULL;
    for (i=0, sp=buf; i<samples_in_buf; i++, sp++) {
    	if (*sp == 0) {
	    if (hole_start == NULL) {
		hole_start = sp;
		hole_size = 1;
	    } else {
	    	hole_size++;
	    }
	} else {
	    /* A non-zero sample: see if this is the end of a hole */
	    if (hole_start != NULL) {
	    	/* This is the end of a hole. Fill it with data from the
		 * audio file, converted to our funny format */
		fill_hole(audio_file, hole_start, hole_size, start + (hole_start - buf));
	    }
	    hole_start = NULL;
	}
    }
    /* If the buffer ends with a hole, fill it */
    if (hole_start != NULL) {
	fill_hole(audio_file, hole_start, hole_size, start + (hole_start-buf));
    }

    /* Now convert our completed data to the required format, one of:
     * mono doubles or same-number-of-channels shorts */
    switch (format) {
    case af_signed:	/* 16-bit with the same number of channels */
    	convert_cached_to_signed((short *)data, buf, samples_in_buf);
        break;
    case af_double:
        convert_cached_to_mono_double((double *)data, buf, samples_in_buf,
				      audio_file->channels);
        break;
    }
    total_frames += samples_in_buf / audio_file->channels;
    return total_frames;
}

/* Fill a hole in our buffer with data from the audio file,
 * in our funny 16-bit format and save the new data in the cache file.
 */
static void
fill_hole(audio_file_t *audio_file,
	  short *hole_start,	/* Where to write newly-cached data into */
	  long hole_samples,	/* How many samples to fill */
	  long start)		/* Frame offset in audio file to read from */
{
    long hole_frames = hole_samples / audio_file->channels;
    long offset;
    int frames_read;
    int frames_to_read = hole_frames;

    if (start + frames_to_read > audio_file->frames) {
    	frames_to_read = audio_file->frames - start;
    }

    if (frames_to_read <= 0)
    	frames_read = 0;
    else
        frames_read = read_audio_file(audio_file, (char *)hole_start,
				      af_signed, audio_file->channels,
				      start, frames_to_read);

    if (frames_read < hole_frames) {
        int i; short *sp;

	/* Fill the missing data with silence */
	for (i=0, sp=hole_start + frames_read * audio_file->channels;
	     i<(hole_frames - frames_read) * audio_file->channels;
	     i++) {
	    *sp++ = (short)0x8000;
	}
    }

    /* Convert the new data in-place to our cached format */
    convert_signed_to_cached(hole_start, hole_start,
    			     frames_read * audio_file->channels);

    /* Save the new data in the cache file */
    offset = start * audio_file->channels * sizeof(short);
    if (lseek(cache, offset, SEEK_SET) != offset) {
	fprintf(stderr, "Warning: Failed to seek to update cache file\n");
    } else {
    	size_t size = frames_read * audio_file->channels * sizeof(short);

	if (write(cache, (char *)hole_start, size) != size) {
	    fprintf(stderr, "Warning: Failed to update cache file\n");
	    /* Mostly harmless */
	}
    }
}

/* Format conversion routines.
 * The first two also work in-place (to == from)
 */
static void
convert_signed_to_cached(short *to, short *from, long len)
{
    int i;
    for (i=len; i>0; i--) {
    	*to++ = *from++ ^ (short)0x8000;
    }
}

static void
convert_cached_to_signed(short *to, short *from, long len)
{
    int i;
    for (i=len; i>0; i--) {
    	*to++ = *from++ ^ (short)0x8000;
    }
}

/* "len" is the number of input samples to convert,
 * taking "channels" samples from "from" for each output sample.
 */
static void
convert_cached_to_mono_double(double *to, short *from, long len, int channels)
{
    int i;
    switch (channels) {
    case 1:
	for (i=0; i<len; i++) {
	    *to++ = (double)(*from++ ^ (short)0x8000) / 32767.0;
	}
	break;
    case 2:
	for (i=0; i<len; i+=2) {
	    *to++ = ((double)(from[0] ^ (short)0x8000) / 32767.0 +
		     (double)(from[1] ^ (short)0x8000) / 32767.0) / 2;
	    from += 2;
	}
	break;
    default:
	for (i=0; i<len; i++) {
	    if (i % channels == 0) {
		/* Start of a new output sample */
		*to = 0.0;
	    }
	    *to += (double)(*from++ ^ (short)0x8000) / 32767.0;
	    if (i % channels == channels - 1) {
		/* End of a new output sample */
		*to++ /= channels;
	    }
	}
	break;
    }
}
