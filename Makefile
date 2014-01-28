CFLAGS=-Wall -Wextra -O2 -D_XOPEN_SOURCE=600

.PHONY: all clean
.DEFAULT: clouddisplayplayer

all: clouddisplayencoder clouddisplayplayer

clean:
	rm -f clouddisplayplayer clouddisplayencoder

clouddisplayencoder: clouddisplayencoder.c
	$(CC) -std=c99 -o $@ $< $(CFLAGS)

clouddisplayplayer: clouddisplayplayer.c
	$(CC) -std=c99 -o $@ $< $(CFLAGS)
