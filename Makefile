test: main.c
	gcc -Wall -g -O3 -o $@ main.c

clean:
	rm test
