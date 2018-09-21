/* audio.h - declarations for audio.c */

/* What the audio subsystem is doing:
 * STOPPED means it has reached the end of the piece and stopped automatically
 * PLAYING means it should be playing audio,
 * PAUSED  means we've paused it or it hasn't started playing yet
 */
#ifndef AUDIO_H

#include "audio_file.h"

enum playing { STOPPED, PLAYING, PAUSED };
extern enum playing playing;

extern void init_audio(audio_file_t *audio_file);
extern void pause_playing(void);
extern void start_playing(void);
extern void stop_playing(void);
extern void continue_playing(void);
extern void set_playing_time(double when);
extern double get_playing_time(void);

#define AUDIO_H
#endif
