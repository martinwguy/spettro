ALL=spettro

OBJS=emotion.o

EMOTION_CFLAGS=`pkg-config --cflags emotion evas ecore ecore-evas eo`
EMOTION_LIBS=`  pkg-config --libs   emotion evas ecore ecore-evas eo`

CFLAGS=-g -Os $(EMOTION_CFLAGS)

all: $(ALL)

install: all
	install $(ALL) ~/bin/

spettro: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EMOTION_LIBS)

clean:
	rm -f $(ALL) *.o core
