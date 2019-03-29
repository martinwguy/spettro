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

/* Declarations for audio_cache.c */

#include "audio_file.h"	/* for af_format_t and audio_file_t */

/* Use a cache on the audio file */
extern void create_audio_cache(audio_file_t *af);

/* Don't use cache on audio file */
extern void no_audio_cache(audio_file_t *af);

/* Read audio from the audio file through the cache.
 * Same interface as read_audio_file
 */
extern int read_cached_audio(audio_file_t *audio_file, char *data,
			     af_format_t format, int channels,
			     int start, int frames_to_read);
