/* barlines.h: Declarations for barlines.c */

#include "gui.h"	/* for color_t */

extern void set_left_bar_time(double when);
extern void set_right_bar_time(double when);
extern bool get_col_overlay(int x, color_t *colorp);
