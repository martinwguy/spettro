#
# Makefile for spettro, a scrolling log-frequency axis spectrogram visualizer
#

PREFIX=/usr/local

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

# In CFLAGS below, define one of USE_EMOTION, USE_SDL and USE_EMOTION_SDL
# USE_EMOTION uses just enlightenment and works best
#	but the audio player doesn't work on 64-bit Ubuntu 16.04;
# USE_EMOTION_SDL gives the best version if USE_EMOTION doesn't work;
# USE_SDL uses SDL for everything so you don't need the Enlightenent toolkit
#	at all but it works worse. If you use this, add -pthread to CFLAGS,
#	-LX11 to OTHER_LIBS and remove EMOTION_CFLAGS and EMOTION_LIBS

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=  `pkg-config --libs   emotion evas ecore ecore-evas eo`

AUDIOFILELIB=-lsndfile  # or -laudiofile and change sndfile.o to audiofile.o

OTHER_LIBS=	$(AUDIOFILELIB) -lSDL -lfftw3 -lm

OPTFLAG=-O -g

CFLAGS= $(EMOTION_CFLAGS) $(OPTFLAG) -DVERSION=\"$(GIT_VERSION)\" \
	-DUSE_EMOTION

SRCS=main.c calc.c window.c spectrum.c interpolate.c colormap.c lock.c \
     speclen.c sndfile.c
OBJS=main.o calc.o window.o spectrum.o interpolate.o colormap.o lock.o \
     speclen.o sndfile.o
# or audiofile.o to use libaudiofile (no Ogg support) instead of libsndfile
# See also AUDIOFILELIB= below

all: spettro

install: all
	install $(ALL) $(PREFIX)/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS) $(OTHER_LIBS)

clean:
	rm -f $(ALL) *.o core

main.o:		calc.h window.h interpolate.h colormap.h audiofile.h Makefile
calc.o:		calc.h window.h spectrum.h audiofile.h Makefile
audiofile.o:	audiofile.h
spectrum.o:	spectrum.h
interpolate.o:	interpolate.h
colormap.o:	colormap.h

# Just checks for compiler warnings at present
check:
	@make OPTFLAG="-Os -Wall" clean all | grep -v ^cc
	@make OPTFLAG="-Os -Wall" CC=clang clean all | grep -v ^clang

tags: $(SRCS)
	ctags $(SRCS)
