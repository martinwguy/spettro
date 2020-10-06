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
 * audio.c - Audio-playing functions
 */

#include "spettro.h"
#include "audio.h"

#include "audio_cache.h"
#include "gui.h"
#include "lock.h"
#include "ui.h"

#include <sys/time.h>	/* for gettimeofsay() */


#if EMOTION_AUDIO

#include <Emotion.h>
extern Evas_Object *em;
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);

#elif SDL_AUDIO

#include <SDL.h>
static off_t sdl_start = 0;	/* At what offset in the audio file, in frames,
				 * will we next read samples to play? */
static void sdl_fill_audio(void *userdata, Uint8 *stream, int len);
static unsigned SDL_buffer_size;	/* In sample frames */

#endif

static void set_real_start_time(double when);

enum playing playing = PAUSED;

void
init_audio(audio_file_t *af, char *filename)
{
#if EMOTION_AUDIO
    /* Set audio player callbacks */
    evas_object_smart_callback_add(em, "playback_finished",
				   playback_finished_cb, NULL);

    /* Load the audio file for playing */
    emotion_object_init(em, NULL);
    emotion_object_video_mute_set(em, EINA_TRUE);
    if (emotion_object_file_set(em, filename) != EINA_TRUE) {
	fputs("Couldn't load audio file. Try compiling with -DUSE_EMOTION_SDL in Makefile.am\n", stderr);
	exit(1);
    }
    evas_object_show(em);
#elif SDL_AUDIO
    {
	double sample_rate = af->sample_rate;
	SDL_AudioSpec wavspec;

	wavspec.freq = lrint(sample_rate);
	wavspec.format = AUDIO_S16SYS;
	wavspec.channels = af->channels;
	/* 4096 makes for a visible lag between audio and video, as the video
	 * follows the next af-reading position, which is 0-4096 samples
	 * ahead of what's playing now.
	 * Set it to "secpp" so that we should never get more than one column
	 * behind.
	 */
	if (sample_rate == 0.0) {
	    fprintf(stderr, "Internal error: init_audio() was called before \"sample_rate\" was initialized.\n");
	    exit(1);
	}
	wavspec.samples = lrint(secpp * sample_rate * af->channels);
	/* SDL sometimes requires a power-of-two buffer,
	 * failing to work if it isn't, so reduce it to such */
	{
	    int places = 0;
	    while (wavspec.samples > 0) {
		wavspec.samples >>= 1;
		places++;
	    }
	    wavspec.samples = 1 << (places - 1);
	}
	SDL_buffer_size = wavspec.samples / af->channels;
	wavspec.callback = sdl_fill_audio;
	wavspec.userdata = af;

	if (SDL_OpenAudio(&wavspec, NULL) < 0) {
	    fprintf(stderr, "Couldn't initialize SDL audio: %s.\n", SDL_GetError());
	    exit(1);
	}
    }
#else
# error "Define one of EMOTION_AUDIO and SDL_AUDIO"
#endif
}

/* Change the audio file */
void
reinit_audio(audio_file_t *af, char *filename)
{
#if EMOTION_AUDIO
    if (emotion_object_file_set(em, filename) != EINA_TRUE) {
	fputs("Couldn't load audio file. Try compiling with -DUSE_EMOTION_SDL in Makefile.am\n", stderr);
	exit(1);
    }
#elif SDL_AUDIO
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    init_audio(af, filename);
#else
# error "Define one of EMOTION_AUDIO and SDL_AUDIO"
#endif
}

#if EMOTION_AUDIO
/*
 * Callback is called when the player gets to the end of the piece.
 *
 * The "playback_started" event is useless because in emotion 0.28 it is
 * delivered when playback of audio finishes (!)
 * An alternative would be the "decode_stop" callback but "playback_finished"
 * is delivered first.
 */
static void
playback_finished_cb(void *data, Evas_Object *obj, void *ev)
{
    stop_playing();
}
#endif

void
pause_audio()
{
#if EMOTION_AUDIO
    emotion_object_play_set(em, EINA_FALSE);
#endif
#if SDL_AUDIO
    SDL_PauseAudio(1);
#endif
    playing = PAUSED;
}

/* Start playing the audio again from it's current position */
void
start_playing()
{
#if EMOTION_AUDIO
    emotion_object_play_set(em, EINA_TRUE);
#endif
#if SDL_AUDIO
    SDL_PauseAudio(0);
#endif
    set_real_start_time(disp_time);
    playing = PLAYING;
}

/* Stop playing because it has arrived at the end of the piece */
void
stop_playing()
{
#if EMOTION_AUDIO
    emotion_object_play_set(em, EINA_FALSE);
#endif
#if SDL_AUDIO
    SDL_PauseAudio(1);
#endif

    /* These settings indicate that the player has stopped at end of track */
    playing = STOPPED;

    if (exit_when_played) {
    	gui_quit_main_loop();
    }
}

void
continue_playing()
{
#if EMOTION_AUDIO
    /* Resynchronise the playing position to the display,
     * as emotion stops playing immediately but seems to throw away
     * the unplayed part of the currently-playing audio buffer.
     */
    emotion_object_position_set(em, disp_time);
    emotion_object_play_set(em, EINA_TRUE);
#endif
#if SDL_AUDIO
    sdl_start = lrint(disp_time * current_sample_rate());
    SDL_PauseAudio(0);
#endif
    set_real_start_time(disp_time);
    playing = PLAYING;
}

/*
 * SDL and Emotion's reporting of the current playing time is grainy,
 * making the scrolling jittery. Instead we calculate it using the
 * real time clock, hoping that it remains in sync.
 */
static double real_start_time;		 /* When we started playing from 0.0,
					  * in seconds from the epoch */
static bool use_real_start_time = FALSE; /* Should we use real_start_time? */

/*
 * Position the audio player at the specified time in seconds.
 */
void
set_playing_time(double when)
{
#if EMOTION_AUDIO
    emotion_object_position_set(em, when);
#endif
#if SDL_AUDIO
    sdl_start = lrint(when * current_sample_rate());
#endif
    set_real_start_time(when);
}

static void
set_real_start_time(double when)
{
    struct timeval tv;

    /* Debugging switch to compare real-time and player-time scrolling */
    if (getenv("SLOPPY") != NULL) { use_real_start_time = FALSE; return; }

    use_real_start_time = (gettimeofday(&tv, NULL) == 0);
    if (!use_real_start_time) {
	perror("Can't get time of day");
    } else {
    	real_start_time = tv.tv_sec + tv.tv_usec * 0.000001 - when;
    }
}

/* Return the audio player's current offset into the audio. */
double
get_playing_time(void)
{
    if (use_real_start_time) {
	struct timeval tv;

	/* Real time keeps on incrementing even if we're not playing */
	if (playing != PLAYING) return get_audio_players_time();

	if (gettimeofday(&tv, NULL) != 0) {
	    perror("Can't get time of day");
	    use_real_start_time = FALSE;
	} else {
	    double now = tv.tv_sec + tv.tv_usec * 0.000001;
	    double retval = now - real_start_time;
	    double audio_players_time = get_audio_players_time();

/* A 16th of a second is noticeable, so resynch if it skews more than a 20th.
 * In practice with SDL2 this happens about once a minute. */
#define MAX_SLOP 0.05

	    /* Check whether the audio player has slipped out of sync */
	    if (DELTA_GT(fabs(retval - audio_players_time), MAX_SLOP)) {
		fprintf(stderr, "Resynching from %.3f to audio player's %.3f\n",
			retval, audio_players_time);
		real_start_time = now - audio_players_time;
	    }
	    return now - real_start_time;
	}
    }

    /* Fallback: ask the audio player what time they are playing at */
    return get_audio_players_time();
}

/* How far into the piece does the audio playing subsystem think it is? */
double
get_audio_players_time(void)
{
#if EMOTION_AUDIO
    /* Empirically, if its playing, e_o_p_g() returns a value on average
     * .0181 seconds ahead of what it's actually playing. */
    return (playing == PLAYING) ? emotion_object_position_get(em) - 0.181
				: emotion_object_position_get(em);
#elif SDL_AUDIO
    /* The current playing time is in sdl_start, counted in frames
     * since the start of the piece. */
    if (playing == PLAYING) {
	/* If its playing, we don't know how much of its last buffer it
	 * has already played, but on average it will be half way through. */
	off_t current = sdl_start - SDL_buffer_size / 2;
	return current < 0 ? 0.0
			   : (double)current / current_sample_rate();
    }
    else
	return (double)(sdl_start) / current_sample_rate();
#endif
}

#if SDL_AUDIO
/*
 * SDL audio callback function to fill the buffer at "stream" with
 * "len" bytes of audio data. We assume they want 16-bit ints.
 *
 * SDL seems to ask for 2048 bytes at a time for a 48kHz mono wav file
 * which is (2048/sizeof(short)) / 48000 = 0.0213, about a fiftieth of
 * a second. Presumably, for stereo this would be a hundredth of a second
 * which is close enough for UI purposes.
 */
static void
sdl_fill_audio(void *userdata, Uint8 *stream, int len)
{
    audio_file_t *af = (audio_file_t *)userdata;
    int channels = af->channels;
    int frames_to_read = len / (sizeof(short) * channels);
    int frames_read;	/* How many were read from the file */

    /* SDL has no "playback finished" callback, so spot it here */
    if (sdl_start >= af->frames) {
        stop_playing();
	/* This may be called by the audio-fill thread,
	 * so don't quit here; tell the main event loop to do so */
	if (exit_when_played) gui_quit_main_loop();
	return;
    }

    frames_read = read_cached_audio(af, (char *)stream,
				    af_signed, channels,
				    sdl_start, frames_to_read);
    if (frames_read == 0) {
	/* End of file. Treat as end of file */
	stop_playing();
	return;
    }
    if (frames_read < 0) {
	/* Some error */
	fprintf(stderr, "Error reading %ld cached frames at %d for the audio player.", sdl_start, len);
	/* Carry on playing: better an audio blip than seizing up */
	/* This never happens because read_cached_audio() always succeeds */
    }

    /* Apply softvol */
    if (softvol != 1.0) {
	int i; short *sp;
	for (i=0, sp=(short *)stream;
	     i < frames_read * channels;
	     i++, sp++) {
	    double value = *sp * softvol;
	    if (DELTA_LT(value, -32767.0) || DELTA_GT(value, 32767.0)) {
		/* Reduce softvol to avoid clipping */
		softvol = 32767.0 / abs(*sp);
		value = *sp * softvol;
printf("The audio would have clipped so I lowered softvol to %g\n", softvol);
	     }

	    /* Plus half a bit of dither? */
	    *sp = (short) lrint(value);
	}
    }

    if (frames_read >= 0) sdl_start += frames_read;
    else sdl_start += len; /* On read errors, pretend it worked */
}
#endif
