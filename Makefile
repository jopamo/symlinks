# Makefile

CC      := gcc
CFLAGS  ?=
LDFLAGS ?=
PREFIX  ?= /usr/local

BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/share/man/man8

TARGET  = symlinks
MANPAGE = symlinks.8

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): symlinks.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

install: all $(MANPAGE)
	install -d -m 755 $(BINDIR)
	install -d -m 755 $(MANDIR)
	install -c -o $(OWNER) -g $(GROUP) -m 755 $(TARGET)  $(BINDIR)/
	install -c -o $(OWNER) -g $(GROUP) -m 644 $(MANPAGE) $(MANDIR)/

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(MANDIR)/$(MANPAGE)

clean:
	rm -f $(TARGET) *.o core
