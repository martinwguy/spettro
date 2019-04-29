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

/* gui.h: Declarations for gui.c */

/* The color for uncalculated areas:  RGB gray */
#if EVAS_VIDEO
typedef unsigned color_t;
typedef unsigned char primary_t;
extern const color_t gray, green, white, black;
#define no_color (0x00000000)	/* EVAS colors are 0xFFRRGGBB */
#elif SDL_VIDEO
typedef unsigned color_t;
typedef unsigned char primary_t;
extern color_t gray, green, white, black;
#define no_color 0xFF000000	/* SDL colors are 0x00RRGGBB */
#endif
#define background gray

#if SDL_MAIN
#define RESULT_EVENT 0
#define SCROLL_EVENT 1
#endif

extern void gui_init(char *filename);
extern void gui_main(void);
extern void gui_quit(void);
extern void gui_quit_main_loop(void);
extern void gui_deinit(void);
extern void gui_fullscreen(void);
extern void gui_update_display(void);
extern void gui_update_rect(int from_x, int from_y, int to_x, int to_y);
extern void gui_update_column(int pos_x);
extern void gui_h_scroll_by(int by);
extern void gui_v_scroll_by(int by);
extern void gui_paint_column(int column, int from_y, int to_y, color_t color);
extern void gui_paint_rect(int from_x, int from_y, int to_x, int to_y, color_t color);
extern void gui_lock(void);
extern void gui_unlock(void);
extern void gui_putpixel(int x, int y, color_t color);
extern bool gui_output_png_file(const char *filename);

#if SDL_MAIN
extern void sdl_main_loop_quit(void);
#endif

extern color_t RGB_to_color(primary_t r, primary_t g, primary_t b);
