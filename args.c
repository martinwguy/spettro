/*
 * args.c - Process command-line arguments
 */

#include "spettro.h"
#include "args.h"

#include "audio_file.h"
#include "barlines.h"
#include "colormap.h"
#include "ui.h"

#include <ctype.h>	/* for tolower() */
#include <errno.h>

#if ECORE_MAIN
#include <Ecore.h>	/* For EFL_VERSION_* */
#endif
#if USE_LIBAV
#include "libavformat/version.h"
#endif

static void print_version(void);

/*
 * Process command-line options, leaving argv pointing at the first filename
 */
void
process_args(int *argcp, char ***argvp)
{
    int argc = *argcp;
    char **argv = *argvp;

    /* Local versions to delay setting until audio length is known */
    double bar_left_time = UNDEFINED;
    double bar_right_time = UNDEFINED;

    for (argv++, argc--;	/* Skip program name */
	 argc > 0 && argv[0][0] == '-';
	 argv++, argc--) {
	int letter = argv[0][1];

switchagain:
	switch (letter) {
	case '-':	/* Handle long args */
	    if (!strcmp(argv[0], "--width")) argv[0] = "-w";
	    else if (!strcmp(argv[0], "--height")) argv[0] = "-h";
	    else if (!strcmp(argv[0], "--jobs")) argv[0] = "-j";
	    else if (!strcmp(argv[0], "--left-bar-line")) argv[0] = "-l";
	    else if (!strcmp(argv[0], "--right-bar-line")) argv[0] = "-r";
	    else if (!strcmp(argv[0], "--fft-freq")) argv[0] = "-f";
	    else if (!strcmp(argv[0], "--start-at")) argv[0] = "-t";
	    else if (!strcmp(argv[0], "--output-png")) argv[0] = "-o";
	    else if (!strcmp(argv[0], "--window")) argv[0] = "-W";
	    else if (!strcmp(argv[0], "--rectangular")) argv[0] = "-WR";
	    else if (!strcmp(argv[0], "--kaiser")) argv[0] = "-WK";
	    else if (!strcmp(argv[0], "--nuttall")) argv[0] = "-WN";
	    else if (!strcmp(argv[0], "--hann")) argv[0] = "-WH";
	    else if (!strcmp(argv[0], "--hamming")) argv[0] = "-WM";
	    else if (!strcmp(argv[0], "--bartlett")) argv[0] = "-WB";
	    else if (!strcmp(argv[0], "--blackman")) argv[0] = "-WL";
	    else if (!strcmp(argv[0], "--dolph")) argv[0] = "-WD";
	    else if (!strcmp(argv[0], "--heatmap")) argv[0] = "-ch";
	    else if (!strcmp(argv[0], "--gray")) argv[0] = "-cg";
	    else if (!strcmp(argv[0], "--grey")) argv[0] = "-cg";
	    else if (!strcmp(argv[0], "--print")) argv[0] = "-cp";
	    else if (!strcmp(argv[0], "--softvol")) argv[0] = "-v";
	    else if (!strcmp(argv[0], "--dyn-range")) argv[0] = "-d";
	    else if (!strcmp(argv[0], "--min-freq")) argv[0] = "-n";
	    else if (!strcmp(argv[0], "--max-freq")) argv[0] = "-x";
	    /* Boolean flags */
	    else if (!strcmp(argv[0], "--autoplay")) argv[0] = "-p";
	    else if (!strcmp(argv[0], "--exit-at-end")) argv[0] = "-e";
	    else if (!strcmp(argv[0], "--fullscreen")) argv[0] = "-F";
	    else if (!strcmp(argv[0], "--piano")) argv[0] = "-k";
	    else if (!strcmp(argv[0], "--guitar")) argv[0] = "-g";
	    else if (!strcmp(argv[0], "--score")) argv[0] = "-s";
	    else if (!strcmp(argv[0], "--show-axes")) argv[0] = "-a";
	    /* Those environment variables */
	    else if (!strcmp(argv[0], "--fps")) argv[0] = "-S";
	    else if (!strcmp(argv[0], "--ppsec")) argv[0] = "-P";
	    /* Flags with no single-letter equivalent */
	    else if (!strcmp(argv[0], "--version")) {
		print_version();
		exit(0);
	    }
	    else goto usage;

	    letter = argv[0][1];

	    goto switchagain;

	/* For flags that take an argument, advance argv[0] to point to it */
	case 'n': case 'x':
	case 'w': case 'h': case 'j': case 'l': case 'r': case 'f': case 't':
	case 'o': case 'W': case 'c': case 'v': case 'd': case 'S': case 'P':
	case 'b':
	    if (argv[0][2] == '\0') {
		argv++, argc--;		/* -j3 */
	    } else {
		 argv[0] += 2;		/* -j 3 */
	    }
	    if (argc < 1 || argv[0][0] == '\0') {
		fprintf(stderr, "-%c what?\n", letter);
		exit(1);
	    }
	}

	switch (letter) {
	/*
	 * Boolean flags
	 */
	case 'p':
	    autoplay = TRUE;
	    break;
	case 'e':
	    exit_when_played = TRUE;
	    break;
	case 'F':
	    fullscreen = TRUE;
	    break;
	case 'k':	/* Draw black and white lines where piano keys fall */
	    piano_lines = TRUE;
	    break;
	case 's':	/* Draw conventional score notation staff lines? */
	    staff_lines = TRUE;
	    guitar_lines = FALSE;
	    break;
	case 'g':	/* Draw guitar string lines? */
	    guitar_lines = TRUE;
	    staff_lines = FALSE;
	    break;
	case 'a':
	    show_axes = TRUE;
	    break;

	/*
	 * Parameters that take an integer argument 
	 */
	case 'w':
	    if ((disp_width = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-w width must be > 0\n");
		exit(1);
	    }
	    break;
	case 'h':
	    if ((disp_height = atoi(argv[0])) <= 0) {
		fprintf(stderr, "-h height must be > 0\n");
		exit(1);
	    }
	    break;
	case 'j':
	    if ((max_threads = atoi(argv[0])) < 0) {
		fprintf(stderr, "-j threads must be >= 0\n");
		exit(1);
	    }
	case 'b':
	    if ((beats_per_bar = atoi(argv[0])) < 0) {
		fprintf(stderr, "-b beats_per_bar must be >= 0\n");
		exit(1);
	    }
	    break;

	/*
	 * Parameters that take a floating point argument
	 */
	case 'n':	/* Minimum frequency */
	case 'x':	/* Maximum frequency */
	case 't':	/* Play starting from time t */
	case 'l':	/* Set left bar line position */
	case 'r':	/* Set right bar line position */
	case 'f':	/* Set FFT frequency */
	case 'v':	/* Set software volume control */
	case 'd':	/* Set dynamic range */
	case 'S':	/* Set scrolling rate */
	case 'P':	/* Set pixel columns per second */
	    errno = 0;
	    {
		char *endptr;
		double arg = strtod(argv[0], &endptr);

		if (errno == ERANGE || endptr == argv[0] || !isfinite(arg)) {
		    fprintf(stderr, "The parameter to -%c must be a floating point number%s.\n",
		    	    letter,
			    tolower(letter) == 'f' ? " in Hz" :
			    letter != 'v' ? " in seconds" :
			    "");
		    exit(1);
		}
		/* They should all be >= 0 */
		if (arg < 0.0) {
		    fprintf(stderr, "The argument to -%c must be positive.\n",
		    	    letter);
		    exit(1);
		}
		/* These must be > 0.
		 * Dynamic range and FPS can be 0, if silly.
		 */
		if (arg == 0.0) switch (letter) {
		case 'f': case 'n': case 'x': case 'P':
		    fprintf(stderr, "The argument to -%c must be positive.\n",
		    	    letter);
		    exit(1);
		default:
		    break;
		}
		if (0 && letter == 'f' && DELTA_LT(arg, 1.0)) {
		    /* Dunno why, but it aborts if < 1.0 */
		    fprintf(stderr, "The FFT frequency must be >= 1.0\n");
		    exit(1);
		}
		switch (letter) {
		case 'n': min_freq = arg;	break;
		case 'm': max_freq = arg;	break;
		case 't': disp_time = arg;	break;
		case 'l': bar_left_time = arg;	break;
		case 'r': bar_right_time = arg; break;
		case 'f': fft_freq = arg;	break;
		case 'v': softvol = arg;	break;
		case 'd': min_db = -arg;	break;
		case 'S': fps = arg;		break;
		case 'P': ppsec = arg;		break;
		}
	    }
	    break;
	/*
	 * Parameters that take a string argument
	 */
	case 'o':
	    output_file = argv[0];
	    break;

	case 'W':
	    switch (tolower(argv[0][0])) {
	    case 'r': window_function = RECTANGULAR; break;
	    case 'k': window_function = KAISER; break;
	    case 'n': window_function = NUTTALL; break;
	    case 'h': window_function = HANN; break;
	    case 'm': window_function = HAMMING; break;
	    case 'b': window_function = BARTLETT; break;
	    case 'l': window_function = BLACKMAN; break;
	    case 'd': window_function = DOLPH; break;
	    default:
		fprintf(stderr, "-W which_window_function?\n\
R = Rectangular\n\
K = Kaiser\n\
N = Nuttall\n\
H = Hann\n\
M = Hamming\n\
B = Bartlett\n\
L = Blackman\n\
D = Dolph (the default)\n");
		exit(1);
	    }
	    break;

	case 'c':			     /* Choose color palette */
	    switch (tolower(argv[0][0])) {
	    case 'h': set_colormap(HEAT_MAP); break;
	    case 'g': set_colormap(GRAY_MAP); break;
	    case 'p': set_colormap(PRINT_MAP); break;
	    default:
		fprintf(stderr, "-c: Which colormap? (heat/gray/print)\n");
		exit(1);
	    }
	    break;

	default:	/* Print Usage message */
	  {
usage:
	    printf(
"Usage: spettro [options] [file]\n\
-p:    Autoplay the file on startup\n\
-e:    Exit when the audio file has played\n\
-h n   Set the window's height to n pixels, default %u\n\
-w n   Set the window's width to n pixels, default %u\n\
-F     Play in fullscreen mode\n\
-n min Set the minimum displayed frequency in Hz\n\
-x min Set the maximum displayed frequency in Hz\n\
-d n   Set the dynamic range of the color map in decibels, default %gdB\n\
-a     Label the vertical frequency axes\n\
-f n   Set the FFT frequency, default %gHz\n\
-t n   Set the initial playing time in seconds\n\
-S n   Set the scrolling rate in frames per second\n\
-P n   Set the number of pixel columns per second\n\
-j n   Set maximum number of threads to use (default: the number of CPUs)\n\
-k     Overlay black and green lines showing frequencies of an 88-note keyboard\n\
-s     Overlay conventional score notation pentagrams as white lines\n\
-g     Overlay lines showing the positions of a classical guitar's strings\n\
-v n   Set the softvolume level to N (>1.0 is louder, <1.0 is softer)\n\
-W x   Use FFT window function x where x starts with\n\
       r for rectangular, k for Kaiser, n for Nuttall, h for Hann\n\
       m for Hamming, b for Bartlett, l for Blackman or d for Dolph, the default\n\
-c map Select a color map: heatmap, gray or print\n\
-o f   Display the spectrogram, dump it to file f in PNG format and quit.\n\
If no filename is supplied, it opens \"audio.wav\"\n\
== Keyboard commands ==\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by a tenth of a screenful\n\
           Shift: by a screenful; Ctrl: by one pixel; Shift-Ctrl: by one second\n\
Up/Down    Pan up/down the frequency axis by a tenth of the graph's height\n\
           (by a screenful if Shift is held; by one pixel if Control is held)\n\
PgUp/PgDn  Pan up/down the frequency axis by a screenful, like Shift-Up/Down\n\
X/x        Zoom in/out on the time axis\n\
Y/y        Zoom in/out on the frequency axis\n\
Plus/Minus Zoom both axes\n\
c          Flip between color maps: heat map - grayscale - gray for printing\n\
Star/Slash Change the dynamic range by 6dB to brighten/darken the quiet areas\n\
b/d        The same as star/slash (meaning \"brighter\" and \"darker\")\n\
f/F        Halve/double the length of the sample taken to calculate each column\n\
R/K/N/H    Set the FFT window function to Rectangular, Kaiser, Nuttall or Hann\n\
M/B/L/D    Set the FFT window function to Hamming, Bartlett, Blackman or Dolph\n\
a          Toggle the frequency axis legend\n\
k          Toggle the overlay of 88 piano key frequencies\n\
s          Toggle the overlay of conventional staff lines\n\
g          Toggle the overlay of classical guitar strings' frequencies\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
9/0        Decrease/increase the soft volume control\n\
t          Show the current playing time on stdout\n\
o          Output the current screen into a PNG file\n\
p          Show the playing time and settings on stdout\n\
Crtl-l     Redraw the display from cached FFT results\n\
Crtl-r     Empty the result cache and redraw the display from the audio data\n\
", disp_width, disp_height,-min_db, fft_freq);
#if SDL_VIDEO
# if SDL1
puts("\
Ctrl-f     Flip full-screen mode");
# endif
#endif
puts("\
q/Ctrl-C/Esc   Quit");
	    exit(1);
	  }
	}
    }

    if (bar_left_time != UNDEFINED) {
	set_left_bar_time(bar_left_time);
    }
    if (bar_right_time != UNDEFINED) {
	set_right_bar_time(bar_right_time);
    }

    *argcp = argc;
    *argvp = argv;
}

static void
print_version()
{
    printf("Spettro version %s built with", VERSION);
#if USE_EMOTION || USE_EMOTION_SDL
    printf(" Enlightenment %d.%d", EFL_VERSION_MAJOR, EFL_VERSION_MINOR);
#endif
#if USE_EMOTION_SDL
    printf(",");
#endif
#if USE_SDL || USE_EMOTION_SDL
# if SDL1
    printf(" SDL 1.2");
# elif SDL2
    printf(" SDL 2.0");
# endif
#endif
    printf(" and ");
#if USE_LIBAUDIOFILE
    printf("libaudiofile %d.%d.%d",
	    LIBAUDIOFILE_MAJOR_VERSION,
	    LIBAUDIOFILE_MINOR_VERSION,
	    LIBAUDIOFILE_MICRO_VERSION);
#elif USE_LIBSNDFILE
    printf("libsndfile");
#elif USE_LIBSOX
    printf("libSoX %s", sox_version());
#elif USE_LIBAV
    printf("FFMPEG's libav %s", AV_STRINGIFY(LIBAVFORMAT_VERSION));
#endif
    printf("\n");
}