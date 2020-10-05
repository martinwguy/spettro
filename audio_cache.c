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
 * audio_cache.c - Stuff to keep any audio we might want in a memory buffer
 *
 * Previously, when an audio fragment was required, for the audio player or
 * for the FFT calculation threads, we would read that fragment of audio from
 * the audio file, with a lock to prevent multiple threads interfering with
 * each other (libmpg123 is thread safe, but we need to seek then read).
 *
 * This resulted in the calc threads and the audio-playing thread locking each
 * other out so much that spettro never used multiple CPUs to full advantage
 * and, with big FFT sizes, the calc threads would lock the audio player out.
 *
 * Instead, now, we always keep any audio that anyone might want to read
 * in a memory buffer, pre-emptively decoded whenever the playing position
 * changes. The audio that anyone might need is the displayed audio plus the
 * lookahead and the lookbehind, plus half the FFT window size.
 *
 * The cached audio needs to be refreshed whenever the displayed audio moves,
 * i.e. on user-interface pans and when the display scrolls while playing,
 * and when the time-zoom changes.
 */

#include "spettro.h"
#include "audio_cache.h"
#include "calc.h"	/* for LOOKAHEAD */
#include "lock.h"
#include "ui.h"

static void shorts_to_mono_floats(float *floats, short *shorts,
				  off_t frames, int nchannels);

/* We keep the cached audio in two formats: */
static short *audio_cache_s = NULL;	/* 16-bit all channels for audio */
static float *audio_cache_f = NULL;	/* 32-bit mono floats for FFT threads */

static off_t audio_cache_start = 0;	/* Where the cache starts in sample frames
				 	 * from the start of the audio file */
static off_t audio_cache_size = 0;	/* Size of cache in sample frames */

/*
 * read_cached_audio(): Same interface as read_audio_file().
 * Returns 0 if we are at end-of-file or a negative number if the audio
 * cache is mispositioned (which "should" never happen, but can if playing
 * position changes by a page but screen hasn't scrolled yet, or if a calc
 * takes so long to get done that time has aleady noved on).
 */

int
read_cached_audio(audio_file_t *af, char *data,
		  af_format_t format, int channels,
		  off_t start, int frames_to_read)
{
    int frames_written = 0;
    size_t framesize;

    switch (format) {
    case af_float: framesize = sizeof(float); break;
    case af_signed: framesize = sizeof(short) * channels; break;
    default: abort();
    }

    /* Deal with start < 0 and fill with silence */
    if (start < 0) {
	int nframes = MIN(-start, frames_to_read);
	memset(data, 0, nframes * framesize);
	start += nframes; data += nframes * framesize;
	frames_to_read -= nframes;
	frames_written += nframes;
	if (frames_to_read == 0) return frames_written;
    }

    /* Are we at end-of-file? */
    if (start >= current_audio_file()->frames) return frames_written;

    /* Does it read past end-of-file? If so, fill the end with silence */
    if (start + frames_to_read > current_audio_file()->frames) {
	int nframes = start + frames_to_read - current_audio_file()->frames;
	frames_to_read -= nframes;
	memset(data + (frames_to_read * framesize), 0, nframes * framesize);
	frames_written += nframes;
    }

    /* Check that the cache is positioned correctly.
     *
     * It can be wrong if the user pans in time and an old calculation thread
     * tries to read audio from the old position, or if they pan by more than
     * half a screenful and the audio-playing thread callback happens before
     * the new data has been decoded.
     */
    lock_audio_cache();

    /* 1) starting before the start of the cache */
    if (start < audio_cache_start) {
	/* zero the missing first part, then copy the rest from the cache */
	if (start + frames_to_read > audio_cache_start) {
	    /* There is an overlap between the cache and the required region so
	     * fill the non-overlapping region with silence
	     */
	    int frames = audio_cache_start - start;

	    switch (format) {
	    case af_float:
		 memset(data, 0, frames * sizeof(float));
		 data += frames * sizeof(float);
		 break;
	    case af_signed:
		 memset(data, 0, frames * channels * sizeof(short));
		 data += frames * channels * sizeof(short);
		 break;
	    }
	    frames_written += frames;
	    frames_to_read -= frames;
	    start = audio_cache_start;
	} else {
	    /* There is no overlap. Fill with silence */
	    unlock_audio_cache();

	    switch (format) {
	    case af_float:
		memset(data, 0, frames_to_read * sizeof(float));
		data += frames_to_read * sizeof(float);
		break;
	    case af_signed:
		memset(data, 0, frames_to_read * channels * sizeof(short));
		data += frames_to_read * channels * sizeof(short);
		break;
	    }
	    return frames_to_read;
	}
    }

    /* 2) Going past the end of the cache */
    if (start + frames_to_read > audio_cache_start + audio_cache_size) {
	/* If there is an overlap, zero the missing end part then copy
	 * the rest from the cache */
	if (start < audio_cache_start + audio_cache_size) {
	    /* Size of the overlapping region in frames */
	    int overlap = audio_cache_start + audio_cache_size - start;
	    /* How many frames to fill with silence */
	    int frames = frames_to_read - overlap;

	    switch (format) {
		int framesize;
	    case af_float:
	    	framesize = sizeof(float);
		memset(data + overlap * framesize, 0, frames * framesize);
		break;
	    case af_signed:
	    	framesize = channels * sizeof(short);
		memset(data + overlap * framesize, 0, frames * framesize);
		break;
	    }
	    frames_written += frames;
	    frames_to_read -= frames;	/* == overlap */
	} else {
	    /* There is no overlap. Fill with silence */
	    unlock_audio_cache();

	    switch (format) {
	    case af_float:
		 memset(data, 0, frames_to_read * sizeof(float));
		 data += frames_to_read * sizeof(float);
		 break;
	    case af_signed:
		 memset(data, 0, frames_to_read * channels * sizeof(short));
		 data += frames_to_read * channels * sizeof(short);
		 break;
	    }
	    return frames_to_read;
	}
    }

    /* Copy from the cache to the audio buffer */
    switch (format) {
    case af_float:
	memcpy(data, audio_cache_f + (start - audio_cache_start),
	       frames_to_read * sizeof(float));
	frames_written += frames_to_read;
	break;
    case af_signed:
	memcpy(data, audio_cache_s + (start - audio_cache_start) * channels,
	       frames_to_read * channels * sizeof(short));
	frames_written += frames_to_read;
	break;
    }

    unlock_audio_cache();

    return(frames_written);
}

/*
 * Make the cached portion of the audio reflect the current settings:
 * the displayed portion of the audio with lookahead and lookbehind
 * plus and minus half the FFT window size at each end.
 *
 * This should only be called from the main thread.
 */
void
reposition_audio_cache()
{
    /* Where the audio cache will start, in frames from start of audio file */
    off_t new_cache_start = lrint(floor(
    	(disp_time - (disp_width/2 + LOOKAHEAD) * secpp - 1/fft_freq/2) * current_sample_rate()
    ));

    /* How big the new cache will be, in seconds */
    double new_cache_time = (disp_width + LOOKAHEAD * 2) * secpp + 1/fft_freq;

    /* How big the new cache will be, in sample frames */
    off_t new_cache_size = lrint(ceil(new_cache_time * current_sample_rate()));

    int nchannels = current_audio_file()->channels;	/* Local copy */

    bool refill = FALSE;			/* Read the whole buffer? */

    /* When new and old buffers overlap, this is the region that needs filling
     * from the audio file */
    off_t fill_start, fill_size;

    lock_audio_cache();

    /* In the rare case of the size of the interesting area changing,
     * due to time zooming or window size change, don't bother trying
     * to preserve the overlapping region. It's just too difficult.
     */
    if (audio_cache_size != 0 && audio_cache_size != new_cache_size) {
	free(audio_cache_s);
	free(audio_cache_f);
	audio_cache_size = 0;
    }

    /* If this is the first call, allocate the cache buffers */
    if (audio_cache_size == 0) {
	audio_cache_s = Malloc(new_cache_size * sizeof(short) * nchannels);
	audio_cache_f = Malloc(new_cache_size * sizeof(float));
	audio_cache_size = new_cache_size;
	refill = TRUE;
    }

    /* Calculate which part we have to update */

    /* Already in the right place? */
    if (!refill && new_cache_start == audio_cache_start) {
	unlock_audio_cache();
	return;
    }
    
    /* Moving forward with overlap? Move the overlapping data left and
     * fill in the last bit */
    if (!refill && new_cache_start > audio_cache_start &&
		   new_cache_start < audio_cache_start + audio_cache_size) {
	/* How many frames we move by, and how many frames overlap */ 
	int move_by = new_cache_start - audio_cache_start;
	int overlap = audio_cache_size - move_by;

	memmove(audio_cache_s, audio_cache_s + move_by * nchannels,
		overlap * sizeof(short) * nchannels);
	memmove(audio_cache_f, audio_cache_f + move_by,
		overlap * sizeof(float));
	audio_cache_start += move_by;
	fill_start = audio_cache_start + overlap;
	fill_size = move_by;
    } else
    /* Moving backward with overlap? Move the overlapping data right and
     * fill in the first bit */
    if (!refill && new_cache_start < audio_cache_start &&
		   new_cache_start > audio_cache_start - audio_cache_size) {
	/* How many frames we move by, and how many frames overlap */ 
	int move_by = audio_cache_start - new_cache_start;
	int overlap = audio_cache_size - move_by;

	memmove(audio_cache_s + move_by * nchannels , audio_cache_s,
		overlap * sizeof(short) * nchannels);
	memmove(audio_cache_f + move_by, audio_cache_f,
		overlap * sizeof(float));
	audio_cache_start -= move_by;
	fill_start = audio_cache_start;
	fill_size = move_by;
    } else {
	/* No overlap. Read the whole buffer. */
	audio_cache_start = new_cache_start;
	fill_start = audio_cache_start;
	fill_size = audio_cache_size;
    }

    /* Read 16-bit nchannel shorts into the buffer
     * and convert those to mono doubles */
    {
	/* Where the region to fill starts, relative to the cache start */
	off_t fill_offset = fill_start - audio_cache_start;  /* in frames */
	int r = read_audio_file(current_audio_file(),
				(char *)(audio_cache_s + fill_offset * nchannels),
				af_signed, nchannels, fill_start, fill_size);
	if (r != fill_size) {
	    fprintf(stderr, "Failed to fill the audio cache with %ld frames; got %d.\n",
		    fill_size, r);
	}
	shorts_to_mono_floats(audio_cache_f + fill_offset,
			      audio_cache_s + fill_offset * nchannels,
			      r, nchannels);
	/* If the read was short or erroneous, fill the unwritten space with silence */
	if (r < 0) r = 0;
	memset(audio_cache_f + fill_offset + r, 0, (fill_size - r) * sizeof(double));
	memset(audio_cache_s + fill_offset + r, 0, (fill_size - r) * sizeof(short) * nchannels);
    }

    unlock_audio_cache();
}

/* Convert 16-bit nchannel shorts to mono floats */
static void
shorts_to_mono_floats(float *floats, short *shorts, off_t frames, int nchannels)
{
    float *fp; short *sp;
    off_t todo;

    sp=shorts, fp = floats, todo = frames;
    switch (nchannels) {
    case 1:
	while (todo > 0) {
	    *fp++ = (float)(*sp++) / 32768.0;
	    todo--;
	}
	break;
    case 2:
	while (todo > 0) {
	    *fp++ = (float)(sp[0] + sp[1]) / 65536.0;
	    sp += 2; todo--;
	}
	break;
    default:
	while (todo > 0) {
	    int i;
	    int total = 0;

	    for (i = 0; i < nchannels; i++) total += *sp++;
	    *fp++ = (float)total / (32768 * nchannels);
	    todo--;
	}
    }
}

/* Dump the audio cache as a WAV file */
void
dump_audio_cache()
{
    SF_INFO  sfinfo;
    SNDFILE *sf;

    sfinfo.samplerate = current_sample_rate();
    sfinfo.channels = current_audio_file()->channels;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    sf = sf_open("audio_cache.wav", SFM_WRITE, &sfinfo);
    if (sf == NULL) {
	fprintf(stderr, "Cannot create audio_cache.wav\n");
	return;
    }

    if (sf_writef_short(sf, audio_cache_s, audio_cache_size) != audio_cache_size) {
	fprintf(stderr, "Failed to write audio cache\n");
    }

    sf_close(sf);
}
