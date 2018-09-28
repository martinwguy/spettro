#
# Makefile.am for spettro, a scrolling log-frequency axis spectrogram visualizer
#

# To choose the video- and audio-driving libraries,
# define USE_EMOTION, USE_SDL or USE_EMOTION_SDL in AM_CFLAGS.
# USE_EMOTION uses only Enlightenment and works best
#	but the audio player doesn't work on 64-bit Ubuntu 16.04.
# USE_EMOTION_SDL gives the best version if Emotion's audio is broken.
#	For this, add -lSDL to AM_LDFLAGS.
# USE_SDL uses SDL for everything but works worse.
#	For this, add -pthread to AM_CFLAGS, -lSDL -lX11 to AM_LDFLAGS
#	and remove EMOTION_CFLAGS and EMOTION_LIBS from both.
#
# To choose the audio file-reading/decoding library, use
# USE_LIBSNDFILE in AM_CFLAGS and use -lsndfile in AM_LDFLAGS
# USE_LIBAUDIOFILE in AM_CFLAGS and use -laudiofile in AM_LDFLAGS
# The main difference is that libaudiofile can't read Ogg files.
# If -DUSE_LIBAUDIOFILE, change -lsndfile to -laudiofile in AM_LDFLAGS
#
# We add -march=native, not only for best speed on your CPU, but also because
# gcc's and clang's defaults generate "illegal instruction" on AMD Sempron.

AM_CFLAGS = -DUSE_SDL -DUSE_LIBSNDFILE $(EMOTION_CFLAGS) $(SDL_CFLAGS) \
	    -march=native
AM_LDFLAGS = -lsndfile $(EMOTION_LIBS) $(SDL_LIBS) -lfftw3 -lm

bin_PROGRAMS = spettro

desktopdir = $(datadir)/appllications
desktop_DATA = spettro.desktop
icondir = $(datadir)/icons
icon_DATA = spettro.png

spettro_SOURCES = main.c config.h spettro.h \
	audio.c audio_file.c cache.c calc.c colormap.c interpolate.c key.c \
	lock.c mouse.c overlay.c scheduler.c speclen.c spectrum.c timer.c \
	window.c \
	audio.h audio_file.h cache.h calc.h colormap.h interpolate.h key.h \
	lock.h mouse.h overlay.h scheduler.h speclen.h spectrum.h timer.h \
	window.h

main.o: Makefile
audio.o: Makefile
calc.o: Makefile
key.o: Makefile
lock.o: Makefile
mouse.o: Makefile
timer.o: Makefile
audio_file.o: Makefile
scheduler.o: Makefile

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas`
EMOTION_LIBS=  `pkg-config --libs   emotion evas ecore ecore-evas`
SDL_CFLAGS=`sdl-config --cflags` -pthread
SDL_LIBS=  `sdl-config --libs` -lX11 -pthread

tags:
	ctags $(spettro_SOURCES)

mrproper: clean
	rm -f tags Makefile Makefile.in aclocal.m4 compile config.log \
		config.guess sonfig.sub ltmain.sh stamp-h1 test-driver \
		config.status configure depcomp install-sh missing \
		configure.h*
	rm -rf .deps autom4te.cache
