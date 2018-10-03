/* gui.h: Declarations for gui.c */

/* The color for uncalculated areas:  RGB gray */
#if EVAS_VIDEO
extern const unsigned background;	/* 50% grey */
extern const unsigned green;
#elif SDL_VIDEO
extern unsigned background, green;
#endif

#if SDL_MAIN
#define RESULT_EVENT 0
#define SCROLL_EVENT 1
#endif

extern void gui_init(char *filename);
extern void gui_main(void);
extern void gui_quit(void);
extern void gui_deinit(void);
extern void gui_update_display(void);
extern void gui_update_column(int pos_x);
extern void gui_scroll_by(int by);
extern void gui_background(int column);
extern void gui_paint_column(int column, unsigned int color);
extern void gui_lock(void);
extern void gui_unlock(void);
extern void gui_putpixel(int x, int y, unsigned char *color);
