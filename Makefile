CFLAGS=-Wall -Wextra -O1 -D_XOPEN_SOURCE=600
ENCODER_CFLAGS=$(shell pkg-config --cflags libavformat libavcodec libswscale libavutil | awk '{gsub(/-I/,"-isystem ");print}')
ENCODER_LDFLAGS=$(shell pkg-config --libs libavformat libavcodec libswscale libavutil)
ENCODER_CFLAGS=$(shell pkg-config --cflags libavformat libavcodec libswscale libswresample libavutil | awk '{gsub(/-I/,"-isystem ");print}')
ENCODER_LDFLAGS=$(shell pkg-config --libs libavformat libavcodec libswscale libswresample libavutil)
PLAYER_CFLAGS=$(shell pkg-config --cflags libavformat libavcodec libswscale libswresample libavutil sdl | awk '{gsub(/-I/,"-isystem ");print}')
PLAYER_LDFLAGS=$(shell pkg-config --libs libavformat libavcodec libswscale libswresample libavutil sdl)

.PHONY: all clean

all: clouddisplayencoder clouddisplayplayer

clean:
	rm -f clouddisplayplayer clouddisplayencoder

clouddisplayencoder: src/clouddisplayencoder.c
	$(CC) -std=c99 $(CFLAGS) $(ENCODER_CFLAGS) $< $(ENCODER_LDFLAGS) -o $@

clouddisplayplayer: src/clouddisplayplayer.c
	$(CC) -std=c99 $(CFLAGS) $(PLAYER_CFLAGS) $< $(PLAYER_LDFLAGS) -o $@

