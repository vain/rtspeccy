EXECUTABLE = rtspeccy

CFLAGS += -Wall -Wextra
LDLIBS += -lm -lglut -lGL -lasound -lfftw3

INSTALL=install
INSTALL_PROGRAM=$(INSTALL)
INSTALL_DATA=$(INSTALL) -m 644

prefix=/usr/local
exec_prefix=$(prefix)
bindir=$(exec_prefix)/bin
datarootdir=$(prefix)/share
mandir=$(datarootdir)/man
man1dir=$(mandir)/man1


.PHONY: all clean install installdirs

all: $(EXECUTABLE)

$(EXECUTABLE): $(EXECUTABLE).c config.h

clean:
	rm -f $(EXECUTABLE)

install: $(EXECUTABLE) installdirs
	$(INSTALL_PROGRAM) $(EXECUTABLE) $(DESTDIR)$(bindir)/$(EXECUTABLE)
	$(INSTALL_DATA) man1/$(EXECUTABLE).1 $(DESTDIR)$(man1dir)/$(EXECUTABLE).1

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir)
