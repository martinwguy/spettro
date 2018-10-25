/*
 * colormap.h: Header file for colormap.c
 */

extern void change_colormap(void);
extern void set_colormap(int which);
extern void
colormap(double value, double min_db, unsigned char *color);

#define SOX_MAP 0
#define SNDFILE_MAP 1
#define GRAY_MAP 2
#define PRINT_MAP 3

#define NMAPS 4
