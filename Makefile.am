#	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
#
#	This program is free software; you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation; either version 2 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program; if not, write to the Free Software
#	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#
# Makefile.am for spettro, a scrolling log-frequency axis spectrogram visualizer
#

# To choose the video-driving and audio-file-reading libraries,
# define one of USE_EMOTION, USE_SDL or USE_EMOTION_SDL in AM_CFLAGS:
# USE_EMOTION	uses only Enlightenment and works best but the audio player
#		doesn't work on 64-bit Ubuntu 16.04 or on Debian.
# USE_EMOTION_SDL uses EFL for everything except sound-playing, for which it
#		uses SDL, giving the best version if Emotion's audio player is
#		broken.
#		It needs both $(SDL_CFLAGS/LIBS) and $(EMOTION_CFLAGS/LIBS)
# USE_SDL	uses SDL for everything. This is now the default.

# ---------------- These are the lines you might want to change ---------------

AM_CFLAGS = -DUSE_SDL -g
AM_CFLAGS += $(SDL_CFLAGS) $(SNDFILE_CFLAGS) $(MPG123_CFLAGS)
AM_LDFLAGS = $(SDL_LIBS) $(SNDFILE_LIBS) $(MPG123_LIBS)

# ---------------------- That's all you need to change ----------------------

# Compulsory flags and libraries.

# We add -march=native, not only for best speed on your CPU, but also because
# gcc and clang's defaults generate "Illegal instruction" on AMD Sempron.

AM_CFLAGS += $(FFTW_CFLAGS) $(PNG_CFLAGS) -march=native -mtune=native -Wall
AM_LDFLAGS += $(FFTW_LIBS) $(PNG_LIBS) -lm

FFTW_CFLAGS=`pkg-config --cflags fftw3f`
FFTW_LIBS=  `pkg-config --libs fftw3f`
PNG_CFLAGS=`pkg-config --cflags libpng`
PNG_LIBS=  `pkg-config --libs libpng`

# Video-driving libraries
SDL_CFLAGS=`sdl2-config --cflags` -pthread
SDL_LIBS=  `sdl2-config --libs` -lX11 -pthread
EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas ecore-x eina`
EMOTION_LIBS=  `pkg-config --libs   emotion evas ecore ecore-evas ecore-x eina`

# Audio file-reading libraries
MPG123_CFLAGS=   `pkg-config --cflags libmpg123`
MPG123_LIBS=     `pkg-config --libs libmpg123`
SNDFILE_CFLAGS=  `pkg-config --cflags sndfile`
SNDFILE_LIBS=    `pkg-config --libs sndfile`

bin_PROGRAMS = spettro

desktopdir = $(datadir)/applications
desktop_DATA = spettro.desktop
icondir = $(datadir)/icons
icon_DATA = spettro.png

spettro_SOURCES = main.c config.h spettro.h \
	alloc.c args.c audio.c audio_cache.c audio_file.c axes.c \
	barlines.c cache.c calc.c colormap.c convert.c do_key.c dump.c \
	gui.c interpolate.c key.c libmpg123.c lock.c mouse.c \
	paint.c overlay.c scheduler.c spectrum.c text.c timer.c \
	ui.c ui_funcs.c window.c \
	\
	alloc.h args.h audio.h audio_cache.h audio_file.h axes.h \
	barlines.h cache.h calc.h colormap.h convert.h do_key.h dump.h \
	gui.h interpolate.h key.h libmpg123.h lock.h mouse.h \
	paint.h overlay.h scheduler.h spectrum.h text.h timer.h \
	ui.h ui_funcs.h window.h

# If the Makefile.am changes, recompile everything to avoid using
# the wrongly-compiled .o files.
# One day, a proper "configure" will make this go away
*.o: Makefile.am

tags:
	ctags $(spettro_SOURCES)

mrproper: clean
	rm -f tags Makefile Makefile.in aclocal.m4 compile config.log \
		config.guess sonfig.sub ltmain.sh stamp-h1 test-driver \
		config.status configure depcomp install-sh missing \
		configure.h*
	rm -rf .deps autom4te.cache
	rm -f swoop.wav
	dh_clean		# Debian stuff

# A test file
#
# Generate a tone sweep from min_freq to max_freq (27.5 to 14080) with an
# amplitude inversely proportional to the frequency. The frequency range is
#	14080/27.5 == 512
# so in 10s the amplitude needs to go down to
#	10*Log10(14080/27.5)dB == 27.0927dB
# Sox fades logarithmically by 100dB in the time you specify so we extend
# this so that it'll be 27.0927dB down after 10 seconds:
# total fade length = 10s * 100/27.0927 == 36.91s
# Sox, with a fade longer than the piece, adds silence, which we then trim.

audio.wav:
	sox -V1 -n -b 16 -e signed $@ synth 10 sine 27.5-14080 \
				  fade l 0 36.91 36.91 trim 0 10

notefreqs: notefreqs.o convert.o ui.o
	$(CC) $(CFLAGS) -o $@ notefreqs.o convert.o ui.o -lm
