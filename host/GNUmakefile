CFLAGS += -Wall

PREFIX ?= /usr/local

all: sendload

clean:
	rm -f sendload

install: sendload
	install -S $< $(PREFIX)/bin/$<

sendload: sendload.c
	$(CC) $(CFLAGS) -o $@ $<
