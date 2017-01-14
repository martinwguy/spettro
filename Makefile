ALL=spettro

EMOTION_FLAGS=`pkg-config --cflags --libs emotion evas ecore ecore-evas eo`

all: $(ALL)

install: all
	install $(ALL) ~/bin/

CFLAGS=-g -Os

spettro: spettro.c
	$(CC) $(CFLAGS) $< -o $@ $(EMOTION_FLAGS)

clean:
	rm -f $(ALL) *.o
