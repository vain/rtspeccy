CFLAGS += -Wall -Wextra -flto -msse3 -mfpmath=sse -O3
LDLIBS += -lm -lglut -lGL -lasound -lfftw3

rtspeccy: rtspeccy.c

.PHONY: clean
clean:
	rm -fv rtspeccy
