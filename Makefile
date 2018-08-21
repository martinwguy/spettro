#
# Makefile for spettro, a scrolling log-frequency axis spectrogram visualizer
#

PREFIX=/usr/local

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

# In CFLAGS below, define one of USE_EMOTION, USE_SDL and USE_EMOTION_SDL
# USE_EMOTION uses just enlightenment and works best
#	but the audio player doesn't work on 64-bit Ubuntu 16.04;
# USE_EMOTION_SDL gives the best version if USE_EMOTION's audio doesn't work;
# USE_SDL uses SDL for everything so you don't need the Enlightenent toolkit
#	at all but it works worse. If you use this, add -pthread to CFLAGS,
#	-LX11 to OTHER_LIBS and remove EMOTION_CFLAGS and EMOTION_LIBS
#
# To choose the audio file reading library:
# USE_LIBAUDIOFILE and AUDIOFILELIB=-laudiofile (can't read Oggs or MP3s) or
# USE_LIBSNDFILE   and AUDILFILELIB=-lsndfile   (can't read MP's)

CFLAGS= $(EMOTION_CFLAGS) $(OPTFLAG) -DVERSION=\"$(GIT_VERSION)\" \
	-DUSE_EMOTION -DUSE_LIBAUDIOFILE -pthread

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=  `pkg-config --libs   emotion evas ecore ecore-evas eo`

AUDIOFILELIB=-laudiofile
# or -laudiofile and in OBJS change sndfile.o to audiofile.o

OTHER_LIBS=	$(AUDIOFILELIB) -lSDL -lfftw3 -lm -lX11

OPTFLAG=-O -g

SRCS=main.c calc.c window.c spectrum.c interpolate.c colormap.c lock.c \
     speclen.c audiofile.c
OBJS=main.o calc.o window.o spectrum.o interpolate.o colormap.o lock.o \
     speclen.o audiofile.o
# or audiofile.o and set AUDIOFILELIB=-laudiofile above

all: spettro

main.o:		calc.h window.h interpolate.h colormap.h audiofile.h Makefile
calc.o:		calc.h window.h spectrum.h audiofile.h Makefile
window.o:	window.h
spectrum.o:	spectrum.h
interpolate.o:	interpolate.h
colormap.o:	colormap.h
lock.o:		lock.h
audiofile.o:	audiofile.h
speclen.o:	speclen.h
audiofile.o:	audiofile.h Makefile

install: all
	install $(ALL) $(PREFIX)/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS) $(OTHER_LIBS)

clean:
	rm -f $(ALL) *.o core

# Just checks for compiler warnings at present
check:
	@make OPTFLAG="-Os -Wall" clean all | grep -v ^cc
	@make OPTFLAG="-Os -Wall" CC=clang clean all | grep -v ^clang

tags: $(SRCS)
	ctags $(SRCS)
