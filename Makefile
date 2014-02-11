CFLAGS=-Wall -Wextra -O2 -D_XOPEN_SOURCE=600
PLAYER_CFLAGS=$(shell pkg-config --cflags libavformat libavcodec libswscale libavutil sdl | awk '{gsub(/-I/,"-isystem ");print}')
PLAYER_LDFLAGS=$(shell pkg-config --libs libavformat libavcodec libswscale libavutil sdl)

.PHONY: all clean

all: clouddisplayencoder clouddisplayplayer

clean:
	rm -f clouddisplayplayer clouddisplayencoder

clouddisplayencoder: src/clouddisplayencoder.c
	$(CC) -std=c99 $(CFLAGS) $(PLAYER_CFLAGS) $< $(PLAYER_LDFLAGS) -o $@

clouddisplayplayer: src/clouddisplayplayer.c
	$(CC) -std=c99 $(CFLAGS) $(PLAYER_CFLAGS) $< $(PLAYER_LDFLAGS) -o $@

