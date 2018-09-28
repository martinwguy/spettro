/*
 * audio.c - Audio-playing functions
 */

#include "spettro.h"
#include "audio.h"
#include "audio_file.h"

#include "main.h"
extern bool exit_when_played;
extern audio_file_t *audio_file;

#include <math.h>

#if EMOTION_AUDIO

#include <Emotion.h>
extern Evas_Object *em;
static void playback_finished_cb(void *data, Evas_Object *obj, void *ev);

#elif SDL_AUDIO

#include <SDL.h>
static unsigned sdl_start = 0;	/* At what offset in the audio file, in frames,
				 * will we next read samples to play? */
static void sdl_fill_audio(void *userdata, Uint8 *stream, int len);

#endif

enum playing playing = PAUSED;

void
init_audio(audio_file_t *audio_file)
{
#if EMOTION_AUDIO
    /* Set audio player callbacks */
    evas_object_smart_callback_add(em, "playback_finished",
				   playback_finished_cb, NULL);
#elif SDL_AUDIO
    {
	SDL_AudioSpec wavspec;

	wavspec.freq = lrint(sample_rate);
	wavspec.format = AUDIO_S16SYS;
	wavspec.channels = audio_file_channels(audio_file);
	wavspec.samples = 4096;
	wavspec.callback = sdl_fill_audio;
	wavspec.userdata = audio_file;

	if (SDL_OpenAudio(&wavspec, NULL) < 0) {
	    fprintf(stderr, "Couldn't initialize SDL audio: %s.\n", SDL_GetError());
	    exit(1);
	}
    }
#else
# error "Define one of EMOTION_AUDIO or SDL_AUDIO"
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

/* Start playing the audio from disp_time into the piece */
void
start_playing()
{
#if EMOTION_AUDIO
    emotion_object_position_set(em, disp_time);
    emotion_object_play_set(em, EINA_TRUE);
#endif
#if SDL_AUDIO
    sdl_start = lrint(disp_time * sample_rate);
    SDL_PauseAudio(0);
#endif
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
    /* Let SDL play last buffer of piece and pause on its own */
    /* SDL_PauseAudio(1); */
#endif

    /* These settings indicate that the player has stopped at end of track */
    playing = STOPPED;
    disp_time = floor(audio_length / step + DELTA) * step;

    if (exit_when_played) {
#if ECORE_MAIN
	ecore_main_loop_quit();
#elif SDL_MAIN
	SDL_Quit();
#endif
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
    sdl_start = lrint(disp_time * sample_rate);
    SDL_PauseAudio(0);
#endif
    playing = PLAYING;
}

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
    sdl_start = lrint(when * sample_rate);
#endif
}

double
get_playing_time(void)
{
#if EMOTION_AUDIO
    return emotion_object_position_get(em);
#elif SDL_AUDIO
    /* The current playing time is in sdl_start, counted in frames
     * since the start of the piece.
     */
    return (double)sdl_start / sample_rate;
#endif
}

#if SDL_AUDIO
/*
 * SDL audio callback function to fill the buffer at "stream" with
 * "len" bytes of audio data.
 */
static void
sdl_fill_audio(void *userdata, Uint8 *stream, int len)
{
    audio_file_t *audiofile = (audio_file_t *)userdata;
    int nchannels = audio_file_channels(audiofile);
    int frames_to_read = len / (sizeof(short) * nchannels);
    int frames_read;	/* How many were read from the file */

    if ((frames_read = read_audio_file(audiofile, (char *)stream,
				       af_signed, nchannels,
				       sdl_start, frames_to_read)) <= 0) {
	/* End of file or read error. Treat as end of file */
	SDL_PauseAudio(1);
    }
    sdl_start += frames_read;

    /* SDL has no "playback finished" callback, so spot it here */
    if (sdl_start >= audio_file_length_in_frames(audiofile)) {
	stop_playing();
    }
}
#endif
