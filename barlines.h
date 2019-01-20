/* barlines.h: Declarations for barlines.c */

#include "gui.h"	/* for color_t */

/* Value for when a bar line is not set */
#define UNDEFINED (-1.0)

extern void set_left_bar_time(double when);
extern void set_right_bar_time(double when);
extern void set_beats_per_bar(int bpb);
extern double get_left_bar_time(void);
extern double get_right_bar_time(void);
extern int get_beats_per_bar(void);
extern bool get_col_overlay(int x, color_t *colorp);
