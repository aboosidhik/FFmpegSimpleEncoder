CFLAGS=-O2 -D_XOPEN_SOURCE=600

clouddisplayencoder: clouddisplayencoder.c
	$(CC) -std=c99 -o $@ $< $(CFLAGS)
