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
#
# To choose the audio file-reading/decoding library, define one of:
# USE_AUDIOFILE, USE_SNDFILE, USE_SOX and USE_AV and change
# SNDFILE_{CFLAGS,LIBS} to AUDIOFILE_*, SNDFILE_*, SOX_* or AV_* accordingly.
# See the file WORKS for how well they work.

# ---------------- These are the lines you might want to change ---------------

AM_CFLAGS = -DUSE_SDL -DUSE_LIBSNDFILE -DUSE_LIBMPG123
AM_CFLAGS += $(SDL_CFLAGS) $(SNDFILE_CFLAGS) $(MPG123_CFLAGS)
AM_LDFLAGS = $(SDL_LIBS) $(SNDFILE_LIBS) $(MPG123_LIBS)

# ---------------------- That's all you need to change ----------------------

# Compulsory flags and libraries.

# We add -march=native, not only for best speed on your CPU, but also because
# gcc and clang's defaults generate "Illegal instruction" on AMD Sempron.

AM_CFLAGS += $(FFTW_CFLAGS) $(PNG_CFLAGS) -march=native -mtune=native -Wall
AM_LDFLAGS += $(FFTW_LIBS) $(PNG_LIBS) -lm

FFTW_CFLAGS=`pkg-config --cflags fftw3`
FFTW_LIBS=  `pkg-config --libs fftw3`
PNG_CFLAGS=`pkg-config --cflags libpng`
PNG_LIBS=  `pkg-config --libs libpng`

# Video-driving libraries
SDL_CFLAGS=`sdl2-config --cflags` -pthread
SDL_LIBS=  `sdl2-config --libs` -lX11 -pthread
EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas ecore-x`
EMOTION_LIBS=  `pkg-config --libs   emotion evas ecore ecore-evas ecore-x`

# Audio file-reading libraries
AUDIOFILE_CFLAGS=`pkg-config --cflags audiofile`
AUDIOFILE_LIBS=  `pkg-config --libs audiofile`
MPG123_CFLAGS=   `pkg-config --cflags libmpg123`
MPG123_LIBS=     `pkg-config --libs libmpg123`
SNDFILE_CFLAGS=  `pkg-config --cflags sndfile`
SNDFILE_LIBS=    `pkg-config --libs sndfile`
SOX_CFLAGS=      `pkg-config --cflags sox`
SOX_LIBS=        `pkg-config --libs sox`
AV_CFLAGS=`pkg-config --cflags libavformat libavcodec libavutil libavfilter`
AV_LIBS=  `pkg-config --libs   libavformat libavcodec libavutil libavfilter`

bin_PROGRAMS = spettro filtering_audio

desktopdir = $(datadir)/applications
desktop_DATA = spettro.desktop
icondir = $(datadir)/icons
icon_DATA = spettro.png

spettro_SOURCES = main.c config.h spettro.h \
	alloc.c args.c audio.c audio_cache.c audio_file.c axes.c \
	barlines.c cache.c calc.c colormap.c convert.c dump.c \
	gui.c interpolate.c key.c libav.c libmpg123.c lock.c mouse.c \
	paint.c overlay.c scheduler.c spectrum.c text.c timer.c \
	ui.c ui_funcs.c window.c \
	\
	alloc.h args.h audio.h audio_cache.h audio_file.h axes.h \
	barlines.h cache.h calc.h colormap.h convert.h dump.h \
	gui.h interpolate.h key.h libav.h libmpg123.h lock.h mouse.h \
	paint.h overlay.h scheduler.h spectrum.h text.h timer.h \
	ui.h ui_funcs.h window.h

filtering_audio_SOURCES = filtering_audio.c libav.c libav.h

# The source files that depend on USE_* defines from config.h
# One day, a proper "configure" will make these go away
main.o: Makefile
args.o: Makefile
audio.o: Makefile
audio_cache.o: Makefile	# Bcos includes audio_file.h
audio_file.o: Makefile
calc.o: Makefile
convert.o: Makefile	# Bcos includes audio_file.h
dump.o: Makefile	# Bcos includes audio_file.h
filtering_audio.o: Makefile
gui.o: Makefile
interpolate.o: Makefile	# Bcos includes audio_file.h
key.o: Makefile
libav.o: Makefile
libmpg123.o: Makefile
lock.o: Makefile
mouse.o: Makefile
overlay.o: Makefile
scheduler.o: Makefile
timer.o: Makefile

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

# Some test files
swoop.wav:
	sox -n -b 16 -e signed $@ synth 10 sine 27.5-14080
