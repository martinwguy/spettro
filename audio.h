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

extern void init_audio(audio_file_t *audio_file, char *filename);
extern void reinit_audio(audio_file_t *audio_file, char *filename);
extern void pause_audio(void);
extern void start_playing(void);
extern void stop_playing(void);
extern void continue_playing(void);
extern void set_playing_time(double when);
extern double get_playing_time(void);
extern double get_audio_players_time(void);

#define AUDIO_H
#endif
