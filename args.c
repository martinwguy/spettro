/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * args.c - Process command-line arguments
 */

#include "spettro.h"
#include "args.h"

#include "audio_file.h"
#include "barlines.h"
#include "colormap.h"
#include "convert.h"
#include "ui.h"

#include <ctype.h>	/* for tolower() */
#include <errno.h>

#if ECORE_MAIN
#include <Ecore.h>	/* For EFL_VERSION_* */
#endif

static void print_version(void);

/* Print a summary of the command-line flags */
static void
usage(void)
{
    printf(
"Usage: spettro [options] file\n\
-p:    Play the file straight away\n\
-e:    Exit when the audio file has played\n\
-h n   Set the window's height to n pixels, default %u\n\
-w n   Set the window's width to n pixels, default %u\n",
				disp_width, disp_height); printf("\
-F     Play in fullscreen mode\n\
-n min Set the minimum displayed frequency in Hz, default %g\n\
-x min Set the maximum displayed frequency in Hz, default %g\n",
				DEFAULT_MIN_FREQ, DEFAULT_MAX_FREQ); printf("\
-d n   Set the dynamic range of the color map in decibels, default %gdB\n",
				DEFAULT_DYN_RANGE); printf("\
-M n   Set the magnitude of the brightest pixel, default %gdB\n",
				DEFAULT_LOGMAX); printf("\
-a     Show the frequency axes\n\
-A     Show the time axis and status line\n\
-f n   Set the FFT frequency in Hz, default %g, minimum %g\n",
				fft_freq, MIN_FFT_FREQ); printf("\
-t n   Set the initial playing time in seconds, mins:secs or H:M:S\n\
-l n   Set the time for the left bar line\n\
-r n   Set the time for the right bar line\n\
-b n   Set the number of beats per bar\n\
-P n   Set how many pixel columns to display per second of audio, default %g\n",
				DEFAULT_PPSEC); printf("\
-R n   Set the scrolling rate in frames per second, default %g\n",
				DEFAULT_FPS); printf("\
-k     Overlay black and white lines showing frequencies of an 88-note keyboard\n\
-s     Overlay conventional score notation pentagrams\n\
-g     Overlay lines showing the frquencies of a classical guitar's strings\n\
-v n   Set the softvolume level to N (>1.0 is louder, <1.0 is softer)\n\
-W x   Use FFT window function x where x starts with\n\
       K for Kaiser, D for Dolph, N for Nuttall, B for Blackman, H for Hann\n\
-m map Select a color map: heatmap, gray or print\n\
-o f   Display the spectrogram, dump it to file f in PNG format and quit\n\
--version  Which version of spettro is this, and which libraries does it use?\n\
--keys Show which key presses do what, and quit\n\
--help This!\n");
}

static void
show_keys()
{
    printf("\
== Keyboard commands ==\n\
Space      Play/Pause/Resume/Restart the audio player\n\
Left/Right Skip back/forward by a tenth of a screenful\n\
           Shift: by a screenful; Ctrl: by one pixel; Shift-Ctrl: by one second\n\
Up/Down    Pan up/down the frequency axis by a tenth of a screenful\n\
           Shift: by a screenful; Ctrl: by one pixel; Shift-Ctrl: by a semitone\n\
PgUp/PgDn  Pan up/down the frequency axis by a screenful, like Shift-Up/Down\n\
X/x        Zoom in/out by a factor of two on the time axis\n\
Y/y        Zoom in/out by a factor of two on the frequency axis\n\
           With Ctrl, zooms in/out by two pixels.\n\
Ctrl +/-   Zoom both axes\n\
m          Cycle through the color maps: heatmap/grayscale/gray for printers\n\
c/C        Decrease/increase the contrast by 6dB (by 1dB if Ctrl is held down)\n\
b/B        Decrease/increase the brightness by 6dB\n\
f/F        Halve/double the length of the sample taken to calculate each column\n\
Ctrl K/D/N/B/H  Set the window function to Kaiser/Dolph/Nuttall/Blackman/Hann\n\
w/W        Cycle forward/backward through the window functions\n\
a          Toggle the frequency axes\n\
A          Toggle the time axis and status line\n\
k          Toggle the overlay of frequencies of a grand piano's 88 keys\n\
s          Toggle the overlay of conventional staff lines\n\
g          Toggle the overlay of frequencies of a classical guitar's strings\n\
l/r        Set the left/right bar markers for an overlay of bar lines\n\
1-9/F1-F12 Set the number of beats per bar (1 or F1 means \"no beat lines\")\n\
0          Remove the bar lines\n\
+/-        Increase/decrease the soft volume control\n\
t          Show the current playing time on stdout\n\
o          Output (save) the current screenful into a PNG file\n\
Ctrl P     Show the playing time and settings on stdout\n\
Ctrl L     Redraw the display from cached FFT results\n\
Ctrl R     Redraw the display by recalculating from the audio data\n\
Ctrl F     Flip full-screen mode\n\
q/Ctrl C/Esc   Quit\n");
    printf("\
== Mouse controls ==\n\
Left/Right click: Set the position of the left/right bar line\n\
");
}

static void
badarg(char *arg)
{
    fprintf(stderr, "Unknown flag: \"%s\". spettro --help gives a list of valid command-line flags.\n", arg);
    exit(1);
}

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
	int letter;

switch_again:
	switch (letter = argv[0][1]) {
	case '-':	/* Handle long args */
	    /* but avoid triggering in the "-p--width" case */
	    if (argv[0][0] != '-') badarg(argv[0] - 1);

	    if (!strcmp(argv[0], "--width")) argv[0] = "-w";
	    else if (!strcmp(argv[0], "--height")) argv[0] = "-h";
	    else if (!strcmp(argv[0], "--jobs")) argv[0] = "-j";
	    else if (!strcmp(argv[0], "--left")) argv[0] = "-l";
	    else if (!strcmp(argv[0], "--right")) argv[0] = "-r";
	    else if (!strcmp(argv[0], "--beats")) argv[0] = "-b";
	    else if (!strcmp(argv[0], "--fft-freq")) argv[0] = "-f";
	    else if (!strcmp(argv[0], "--start")) argv[0] = "-t";
	    else if (!strcmp(argv[0], "--output")) argv[0] = "-o";
	    else if (!strcmp(argv[0], "--window")) argv[0] = "-W";
	    else if (!strcmp(argv[0], "--kaiser")) argv[0] = "-WK";
	    else if (!strcmp(argv[0], "--dolph")) argv[0] = "-WD";
	    else if (!strcmp(argv[0], "--nuttall")) argv[0] = "-WN";
	    else if (!strcmp(argv[0], "--blackman")) argv[0] = "-WB";
	    else if (!strcmp(argv[0], "--hann")) argv[0] = "-WH";
	    else if (!strcmp(argv[0], "--heat")) argv[0] = "-ch";
	    else if (!strcmp(argv[0], "--gray")) argv[0] = "-cg";
	    else if (!strcmp(argv[0], "--grey")) argv[0] = "-cg";
	    else if (!strcmp(argv[0], "--print")) argv[0] = "-cp";
	    else if (!strcmp(argv[0], "--softvol")) argv[0] = "-v";
	    else if (!strcmp(argv[0], "--dyn-range")) argv[0] = "-d";
	    else if (!strcmp(argv[0], "--min-freq")) argv[0] = "-n";
	    else if (!strcmp(argv[0], "--max-freq")) argv[0] = "-x";
	    /* Boolean flags */
	    else if (!strcmp(argv[0], "--autoplay")) argv[0] = "-p";
	    else if (!strcmp(argv[0], "--exit")) argv[0] = "-e";
	    else if (!strcmp(argv[0], "--fullscreen")) argv[0] = "-F";
	    else if (!strcmp(argv[0], "--piano")) argv[0] = "-k";
	    else if (!strcmp(argv[0], "--guitar")) argv[0] = "-g";
	    else if (!strcmp(argv[0], "--score")) argv[0] = "-s";
	    else if (!strcmp(argv[0], "--freq-axis")) argv[0] = "-a";
	    else if (!strcmp(argv[0], "--time-axis")) argv[0] = "-A";
	    /* Those environment variables */
	    else if (!strcmp(argv[0], "--fps")) argv[0] = "-R";
	    else if (!strcmp(argv[0], "--ppsec")) argv[0] = "-P";
	    /* Flags with no single-letter equivalent */
	    else if (!strcmp(argv[0], "--version")) {
		print_version();
		exit(0);
	    } else if (!strcmp(argv[0], "--keys")) {
	    	show_keys();
		exit(0);
	    } else {
	    	/* --help and everything else */
		usage();
		exit(0);
	    }

	    /* Switch on the short-form argument letter */
	    goto switch_again;

	/* For flags that take an argument, advance argv[0] to point to it */
	case 'n': case 'x':
	case 'w': case 'h': case 'j': case 'l': case 'r': case 'f': case 't':
	case 'o': case 'W': case 'm': case 'v': case 'd': case 'R': case 'P':
	case 'b': case 'M':
	    if (argv[0][2] == '\0') {
		argv++, argc--;		/* -j 3 */
	    } else {
		 argv[0] += 2;		/* -j3 */
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
	    goto another_letter;
	case 'e':
	    exit_when_played = TRUE;
	    goto another_letter;
	case 'F':
	    fullscreen = TRUE;
	    goto another_letter;
	case 'k':	/* Draw black and white lines where piano keys fall */
	    piano_lines = TRUE;
	    goto another_letter;
	case 's':	/* Draw conventional score notation staff lines */
	    staff_lines = TRUE;
	    guitar_lines = FALSE;
	    goto another_letter;
	case 'g':	/* Draw guitar string lines */
	    guitar_lines = TRUE;
	    staff_lines = FALSE;
	    goto another_letter;
	case 'a':
	    show_freq_axes = TRUE;
	    goto another_letter;
	case 'A':
	    show_time_axes = TRUE;
	    goto another_letter;

another_letter:
	    /* Allow multiple flags in the same command like argument */
	    if (argv[0][2] != '\0') {
		argv[0]++;
		goto switch_again;
	    }
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
	    /* Let them say 0 to mean "no beats" (which should be 1) */
	    if (beats_per_bar == 0) beats_per_bar = DEFAULT_BEATS_PER_BAR;
	    break;

	/*
	 * Parameters that take a floating point argument
	 */
	case 'n':	/* Minimum frequency */
	case 'x':	/* Maximum frequency */
	case 'f':	/* Set FFT frequency */
	case 'v':	/* Set software volume control */
	case 'd':	/* Set dynamic range */
	case 'R':	/* Set scrolling rate */
	case 'P':	/* Set pixel columns per second */
	case 'M':	/* Set logmax */
	    errno = 0;
	    {
		double arg;

		/* Min and max frequencies can also be "A0" or whatever */
		if ((letter == 'n' || letter == 'x') &&
		    !isnan(arg = note_name_to_freq(argv[0]))) {
		    ;
		} else {
		    char *endptr;

		    arg = strtod(argv[0], &endptr);

		    if (errno == ERANGE || endptr == argv[0] || !isfinite(arg)) {
			fprintf(stderr, "The parameter to -%c must be a ",
					letter);
			switch (letter) {
			case 'n':	/* Minimum frequency */
			case 'x':	/* Maximum frequency */
			    fprintf(stderr, "frequency in Hz or a note name");
			    break;
			case 'f':	/* Set FFT frequency */
			    fprintf(stderr, "frequency in Hz");
			    break;
			case 'M':	/* Set logmax */
			    fprintf(stderr, "value in dB");
			    break;
			case 'd':	/* Set dynamic range */
			    fprintf(stderr, "range in dB");
			    break;
			case 'v':	/* Set software volume control */
			case 'P':	/* Set pixel columns per second */
			case 'R':	/* Set scrolling rate */
			    fprintf(stderr, "floating point number");
			    break;
			}
			fprintf(stderr, ".\n");
			exit(1);
		    }
		}
		/* They should all be >= 0 except for logmax */
		if (arg < 0.0 && letter != 'M') {
		    fprintf(stderr, "The argument to -%c must be positive.\n",
		    	    letter);
		    exit(1);
		}

		/* Place an arbitrary lower limit on the FFT frequency */
		if (letter == 'f' && DELTA_LT(arg, MIN_FFT_FREQ)) {
		    fprintf(stderr, "The FFT frequency must be >= %g\n",
		    	    MIN_FFT_FREQ);
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
		switch (letter) {
		case 'n': min_freq = arg;	break;
		case 'x': max_freq = arg;	break;
		case 'f': fft_freq = arg;	break;
		case 'v': softvol = arg;	break;
		case 'd': dyn_range = arg;	break;
		case 'R': fps = arg;		break;
		case 'P': ppsec = arg;		break;
		case 'M': logmax = arg;		break;
		default: fprintf(stderr, "Internal error: Unknown numeric argument -%c\n", letter);
		}
	    }
	    break;
	/*
	 * Parameters that take a string argument
	 */
	case 't':	/* Play starting from time t */
	case 'l':	/* Set left bar line position */
	case 'r':	/* Set right bar line position */
	    {
		double secs = string_to_seconds(argv[0]);

		if (isnan(secs) || secs < 0.0) {
		    fprintf(stderr, "Time not recognized in -%c %s; ",
			letter, argv[0]);
		    fprintf(stderr, "the maximum is 99:59:59.99 (359999.99 seconds).\n");
		    exit(1);
		}

		switch (letter) {
		    case 't': start_time = secs;	break;
		    case 'l': bar_left_time = secs;	break;
		    case 'r': bar_right_time = secs;	break;
		}
	    }
	    break;

	case 'o':
	    output_file = argv[0];
	    break;

	case 'W':
	    switch (tolower(argv[0][0])) {
	    case 'k': window_function = KAISER; break;
	    case 'n': window_function = NUTTALL; break;
	    case 'h': window_function = HANN; break;
	    case 'b': window_function = BLACKMAN; break;
	    case 'd': window_function = DOLPH; break;
	    default:
		fprintf(stderr, "-W which? Kaiser, Dolph, Nuttall, Blackman or Hann?\n");
		exit(1);
	    }
	    break;

	case 'm':			     /* Choose color map */
	    switch (tolower(argv[0][0])) {
	    case 'h': set_colormap(HEAT_MAP); break;
	    case 'g': set_colormap(GRAY_MAP); break;
	    case 'p': set_colormap(PRINT_MAP); break;
	    default:
		fprintf(stderr, "-m: Which colormap? (heat/gray/print)\n");
		exit(1);
	    }
	    break;

	default:
	    badarg(argv[0]);
	}
    }

    /* Don't call set_*_bar_time because that would trigger repaints before
     * the graphics system is up */
    if (bar_left_time != UNDEFINED) {
	left_bar_time = bar_left_time;
    }
    if (bar_right_time != UNDEFINED) {
	right_bar_time = bar_right_time;
    }

    /* Sanity checks */

    /* Upside-down graphs (tho' it works!) and ranges that are too tiny */
    if (max_freq - min_freq < 1.0) {
	fprintf(stderr, "The maximum frequency must be higher than the minimum!\n");
	exit(1);
    }

     /* Set variables with derived values */
     disp_offset = disp_width / 2;
     min_x = 0; max_x = disp_width - 1;

    /* They must supply at least one filename argument */
    if (argc <= 0) {
	fprintf(stderr, "You must supply at least one audio file name.\n");
	exit(1);
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
    printf("\n");
}
