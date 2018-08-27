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

AM_CFLAGS = -DUSE_EMOTION -DUSE_LIBSNDFILE $(EMOTION_CFLAGS) $(SDL_CFLAGS)
AM_LDFLAGS = -lsndfile $(AUDIOFILELIB) $(EMOTION_LIBS) $(SDL_LIBS) -lfftw3 -lm

bin_PROGRAMS = spettro

spettro_SOURCES = main.c config.h spettro.h \
	audio_file.c calc.c colormap.c interpolate.c lock.c \
	speclen.c spectrum.c window.c \
	audio_file.h calc.h colormap.h interpolate.h lock.h \
	speclen.h spectrum.h window.h

main.o: Makefile
calc.o: Makefile
lock.o: Makefile
audio_file.o: Makefile

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas`
EMOTION_LIBS=  `pkg-config --libs   emotion evas ecore ecore-evas`
SDL_CFLAGS=`sdl-config --cflags` -pthread
SDL_LIBS=  `sdl-config --libs` -lX11 -pthread

tags:
	ctags $(spettro_SOURCES)

mrproper: clean
	rm -f tags Makefile Makefile.in aclocal.m4 compile config.log \
		config.status configure depcomp install-sh missing
	rm -rf .deps autom4te.cache
