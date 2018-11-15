/* axes.h: Declarations for axes.c */

extern void draw_frequency_axes(void);

/* 22050- Five * (digit + blank column), 2 for ".", 2 pixels for tick */
#define FREQUENCY_AXIS_WIDTH (5 * (3 + 1) + 2 + 2)

/* -A0 Two * (letter + blank column), 2 pixels for tick */
#define NOTE_NAME_AXIS_WIDTH (2 + 2 * (1 + 3))
