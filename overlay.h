/*
 * overlay.h - Declarations for overlay.c
 */

/* make_overlay reads main's globals:
 *    double min_freq, max_freq, 
 *    int disp_height,
 *    bool piano_lines, staff_lines
 * So does get_overlay to check that nothing has changed.
 */
void make_overlay(void);
unsigned int get_overlay(int y);
