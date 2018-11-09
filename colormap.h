/*
 * colormap.h: Header file for colormap.c
 */

extern void change_colormap(void);
extern void set_colormap(int which);
extern void
colormap(double value, double min_db, unsigned char *color);

typedef enum {
    HEAT_MAP=0,
    GRAY_MAP,
    PRINT_MAP,
    NMAPS
} colormap_t;
