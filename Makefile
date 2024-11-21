test: main.c
	gcc -Wall -g -O0 -o $@ main.c

clean:
	rm test
