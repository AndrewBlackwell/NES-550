.PHONY: all clean
all: play

play: c8.c
	$(CC) -O2 -o $@ $< -lSDL2 -g ${WARN}

clean:
	rm -f c8
