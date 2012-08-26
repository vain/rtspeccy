EXECUTABLE = rtspeccy

CFLAGS += -Wall -Wextra
LDFLAGS += -lm -lglut -lGL -lasound -lfftw3

INSTALL=install
INSTALL_PROGRAM=$(INSTALL)
INSTALL_DATA=$(INSTALL) -m 644

prefix=/usr/local
exec_prefix=$(prefix)
bindir=$(exec_prefix)/bin
datarootdir=$(prefix)/share
mandir=$(datarootdir)/man
man1dir=$(mandir)/man1


.PHONY: clean install installdirs

$(EXECUTABLE): $(EXECUTABLE).c config.h
	$(CC) $(CFLAGS) -o $(EXECUTABLE) $(EXECUTABLE).c $(LDFLAGS)

clean:
	rm -fv $(EXECUTABLE)

install: $(EXECUTABLE) installdirs
	$(INSTALL_PROGRAM) $(EXECUTABLE) $(DESTDIR)$(bindir)/$(EXECUTABLE)
	$(INSTALL_DATA) man1/$(EXECUTABLE).1 $(DESTDIR)$(man1dir)/$(EXECUTABLE).1

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir)
