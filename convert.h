/* convert.h: Declarations for convert.c */

extern double v_pixel_freq_ratio(void);
extern double pixel_row_to_frequency(int pixel_row);
extern double frequency_to_specindex(double freq);
extern int fft_freq_to_speclen(double fft_freq);
