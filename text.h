/* text.h: Declarations for text.c */

typedef enum {
    LEFT, RIGHT, CENTER,
} alignment_t;

#define TOP LEFT
#define BOTTOM RIGHT

extern void draw_text(char *text, int at_x, int at_y,
		      alignment_t alignment_x, alignment_t alignment_y);