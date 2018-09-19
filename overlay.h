/* overlay.h: Declarations for overlay.c */

extern void make_row_overlay(void);
extern unsigned int get_row_overlay(int y);

extern void set_bar_left_time(double when);
extern void set_bar_right_time(double when);
extern unsigned int get_col_overlay(int x);
