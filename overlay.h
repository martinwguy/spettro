/* overlay.h: Declarations for overlay.c */

#include "gui.h"	/* for color_t */

extern void make_row_overlay(void);
extern bool get_row_overlay(int y, color_t *colorp);
extern void free_row_overlay(void);

extern void set_bar_left_time(double when);
extern void set_bar_right_time(double when);
extern bool get_col_overlay(int x, color_t *colorp);
