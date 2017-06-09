ALL=spettro

OBJS=emotion.o calc.o window.o spectrum.o interpolate.o colormap.o

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=`  pkg-config --libs   emotion evas ecore ecore-evas eo`
OTHER_LIBS=	-laudiofile -lfftw3 -lm

CFLAGS=-g $(EMOTION_CFLAGS) $(OPTFLAG)

all: $(ALL)

install: all
	install $(ALL) ~/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS) $(OTHER_LIBS)

clean:
	rm -f $(ALL) *.o core

emotion.o:	calc.h window.h interpolate.h colormap.h
calc.o:		calc.h window.h spectrum.h
spectrum.o:	spectrum.h
interpolate.o:	interpolate.h
colormap.o:	colormap.h
