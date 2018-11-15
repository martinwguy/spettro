/* convert.h: Declarations for convert.c */

extern double v_pixel_freq_ratio(void);
extern double magindex_to_frequency(int k);
extern double frequency_to_specindex(double freq);
extern int fft_freq_to_speclen(double fft_freq);

#define NOTE_A440	48  /* A above middle C */
#define A0_FREQUENCY	27.5

extern double note_name_to_freq(const char *note);
extern double note_number_to_freq(const int note);
extern int freq_to_magindex(double freq);
