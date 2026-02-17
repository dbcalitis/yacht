CC = gcc
FLAGS = -g -Wextra -Wall -Wpedantic

build: player.c biquad.o
	$(CC) $(FLAGS) -o yacht player.c biquad.o -lasound -lm

biquad.o: biquad.c
	$(CC) -c biquad.c

clean:
	rm *.o yacht
