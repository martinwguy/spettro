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
 * scheduler.h: Function call interface to scheduler.c
 */

#include "calc.h"

extern void start_scheduler(int nthreads);
extern void stop_scheduler(void);
extern void schedule(calc_t *calc);
extern bool there_is_work(void);
extern void drop_all_work(void);
extern calc_t *get_work(void);
extern void reschedule_for_bigger_step(void);
extern void calc_notify(result_t *result);

extern int jobs_in_flight;

#if SDL_MAIN
extern bool sdl_quit_threads;
#endif
