/*
 * colormap.h: Header file for colormap.c
 */

#include "gui.h"	/* for color_t */

extern void change_colormap(void);
extern void set_colormap(int which);
extern color_t colormap(double value, double min_db);

typedef enum {
    HEAT_MAP=0,
    GRAY_MAP,
    PRINT_MAP,
    NUMBER_OF_COLORMAPS
} colormap_t;
