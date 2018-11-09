/* gui.h: Declarations for gui.c */

/* The color for uncalculated areas:  RGB gray */
#if EVAS_VIDEO
typedef unsigned color_t;
typedef unsigned char primary_t;
extern const color_t gray, green, white, black;
#elif SDL_VIDEO
typedef unsigned color_t;
typedef unsigned char primary_t;
extern color_t gray, green, white, black;
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
extern void gui_update_display(void);
extern void gui_update_rect(int pos_x, int pos_y, int width, int height);
extern void gui_update_column(int pos_x);
extern void gui_h_scroll_by(int by);
extern void gui_v_scroll_by(int by);
extern void gui_paint_column(int column, int from_y, int to_y, color_t color);
extern void gui_paint_rect(int from_x, int from_y, int to_x, int to_y, color_t color);
extern void gui_lock(void);
extern void gui_unlock(void);
extern void gui_putpixel(int x, int y, color_t color);
#if SDL_MAIN
extern void sdl_main_loop_quit(void);
#endif

extern color_t RGB_to_color(primary_t r, primary_t g, primary_t b);
