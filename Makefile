#
# Makefile for spettro, a scrolling log-frequency axis spectrogram visualizer
#

ALL=spettro
PREFIX=/usr/local

SRCS=main.c calc.c window.c spectrum.c interpolate.c colormap.c lock.c \
     speclen.c sndfile.c
OBJS=main.o calc.o window.o spectrum.o interpolate.o colormap.o lock.o \
     speclen.o sndfile.o
# or audiofile.o to use libaudiofile (no Ogg support) instead of libsndfile 
# See also AUDIOFILELIB= below

GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=`  pkg-config --libs   emotion evas ecore ecore-evas eo`
AUDIOFILELIB=	-lsndfile
#AUDIOFILELIB=	-laudiofile
OTHER_LIBS=	$(AUDIOFILELIB) -lSDL -lfftw3 -lm

OPTFLAG=-O -g
CFLAGS= $(EMOTION_CFLAGS) $(OPTFLAG) -DVERSION=\"$(GIT_VERSION)\" \
	-DSDL_AUDIO
LDFLAGS=

all: $(ALL)

install: all
	install $(ALL) $(PREFIX)/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS) $(OTHER_LIBS)

clean:
	rm -f $(ALL) *.o core

main.o:		calc.h window.h interpolate.h colormap.h audiofile.h
calc.o:		calc.h window.h spectrum.h audiofile.h
audiofile.o:	audiofile.h
spectrum.o:	spectrum.h
interpolate.o:	interpolate.h
colormap.o:	colormap.h

# Just checks for compiler warnings at present
check:
	@make OPTFLAG="-Os -Wall" clean all | grep -v ^cc
	@make OPTFLAG="-Os -Wall" CC=clang clean all | grep -v ^clang

tags:
	ctags $(SRCS)
