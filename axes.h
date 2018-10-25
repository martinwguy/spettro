/* axes.h: Declarations for axes.c */

extern void draw_frequency_axis(void);

/* Five * (digit + blank column), 2 pixels for tick */
#define FREQUENCY_AXIS_WIDTH (5 * (3 + 1) + 2)
