ALL=spettro

OBJS=emotion.o calc.o window.o spectrum.o interpolate.o colormap.o audiofile.o

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=`  pkg-config --libs   emotion evas ecore ecore-evas eo`
OTHER_LIBS=	-laudiofile -lfftw3 -lm

OPTFLAG=-O
CFLAGS=-g $(EMOTION_CFLAGS) $(OPTFLAG)

all: $(ALL)

install: all
	install $(ALL) ~/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS) $(OTHER_LIBS)

clean:
	rm -f $(ALL) *.o core

emotion.o:	calc.h window.h interpolate.h colormap.h audiofile.h
calc.o:		calc.h window.h spectrum.h audiofile.h
audiofile.o:	audiofile.h
spectrum.o:	spectrum.h
interpolate.o:	interpolate.h
colormap.o:	colormap.h

# Just checks for compiler warnings at present
check:
	@make OPTFLAG="-Os -Wall" clean all | grep -v ^cc
	@make OPTFLAG="-Os -Wall" CC=clang clean all | grep -v ^clang
