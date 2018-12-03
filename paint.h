/*
 * paint.h: Declarations for paint.c
 */

#ifndef PAINT_H

#include "calc.h"		/* for result_t */

extern void do_scroll(void);
extern void repaint_display(bool repaint_all);
extern void repaint_columns(int from_x, int to_x, int from_y, int to_y, bool refresh_only);
extern void repaint_column(int column, int min_y, int max_y, bool refresh_only);
extern void paint_column(int pos_x, int min_y, int max_y, result_t *result);

#define PAINT_H
#endif
