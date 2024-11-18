test: main.c
	gcc -Wall -g -O2 -o $@ main.c

clean:
	rm test
