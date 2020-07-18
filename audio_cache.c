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
 */

int
read_cached_audio(char *data,
		  af_format_t format, int channels,
		  int start, int frames_to_read)
{
    /* Check that the cache is positioned correctly */
    /* It can be wrong if the user pans the time and an old calculation thread
     * tries to read audio from the old position */
    if (start < audio_cache_start ||
        start + frames_to_read > audio_cache_start + audio_cache_size) {
	fprintf(stderr, "Audio cache is mispositioned.\n");
	fprintf(stderr, "start=%ld size=%ld want %d frames from %d\n",
		audio_cache_start, audio_cache_size, frames_to_read, start);
	return 0;
    }

    switch (format) {
    case af_float:
	memcpy(data, audio_cache_f + (start - audio_cache_start),
	       frames_to_read * sizeof(float));
	break;
    case af_signed:
	memcpy(data, audio_cache_s + (start - audio_cache_start) * channels,
	       frames_to_read * channels * sizeof(short));
	break;
    }

    return(frames_to_read);
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
    off_t new_cache_start = lrint(floor(
    	(disp_time - (disp_width/2 + LOOKAHEAD) * secpp - 1/fft_freq/2) * current_sample_rate()
    ));
    double new_cache_time = (disp_width + LOOKAHEAD * 2) * secpp + 1/fft_freq;
    off_t new_cache_size = lrint(ceil(new_cache_time * current_sample_rate()));
    int nchannels = current_audio_file()->channels;

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
    }

    /* Read the whole buffer as 16-bit nchannel shorts
     * and convert those to mono doubles */
    audio_cache_start = new_cache_start;
    {	int n;
	n = read_audio_file((char *)audio_cache_s,
			af_signed, nchannels,
    			audio_cache_start,
			audio_cache_size);
	if (n != audio_cache_size) {
	    fprintf(stderr, "Failed to fill the audio cache at %ld. Wanted %ld, got %d.\n",
	    	    audio_cache_start, audio_cache_size, n);
	    exit(1);
	}
	shorts_to_mono_floats(audio_cache_f, audio_cache_s,
			      audio_cache_size, nchannels);
    }
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
