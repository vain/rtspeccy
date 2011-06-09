CFLAGS += -Wall -Wextra
LDLIBS += -lm -lglut -lGL -lasound -lfftw3

rtspeccy: rtspeccy.c

.PHONY: clean
clean:
	rm -fv rtspeccy
