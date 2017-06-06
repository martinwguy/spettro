ALL=spettro

OBJS=emotion.o calc.o window.o spectrum.o

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=`  pkg-config --libs   emotion evas ecore ecore-evas eo`
OTHER_LIBS=	-laudiofile -lfftw3 -lm

CFLAGS=-g $(EMOTION_CFLAGS) $(OPTFLAG) -Dbool=int

all: $(ALL)

install: all
	install $(ALL) ~/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS) $(OTHER_LIBS)

clean:
	rm -f $(ALL) *.o core
