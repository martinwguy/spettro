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
 * audio_cache.h - include file for clients of audio_cache.c
 */

#ifndef AUDIO_CACHE_H

#include "audio_file.h"	/* for af_format_t */

extern int read_cached_audio(char *data, af_format_t format, int channels,
			     int start, int frames_to_read);

extern void reposition_audio_cache(void);

#define AUDIO_CACHE_H 1
#endif
