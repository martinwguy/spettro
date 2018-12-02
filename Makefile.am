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

AM_CFLAGS = -DUSE_SDL -DUSE_LIBSNDFILE
AM_CFLAGS += $(SDL_CFLAGS) $(SNDFILE_CFLAGS)
AM_LDFLAGS = $(SDL_LIBS) $(SNDFILE_LIBS)

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
	alloc.c audio.c audio_cache.c audio_file.c axes.c \
	barlines.c cache.c calc.c colormap.c convert.c \
	gui.c interpolate.c key.c libav.c lock.c mouse.c \
	overlay.c scheduler.c spectrum.c text.c timer.c \
	ui_funcs.c window.c \
	\
	alloc.h audio.h audio_cache.h audio_file.h axes.h \
	barlines.h cache.h calc.h colormap.h convert.h \
	gui.h interpolate.h key.h libav.h lock.h mouse.h \
	overlay.h scheduler.h spectrum.h text.h timer.h \
	ui_funcs.h window.h

filtering_audio_SOURCES = filtering_audio.c libav.c libav.h

# The source files that depend on USE_* defines from config.h
# One day, a proper "configure" will make these go away
main.o: Makefile
audio.o: Makefile
calc.o: Makefile
gui.o: Makefile
key.o: Makefile
lock.o: Makefile
mouse.o: Makefile
overlay.o: Makefile
timer.o: Makefile
audio_file.o: Makefile
scheduler.o: Makefile
filtering_audio.o: Makefile
libav.o: Makefile

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
