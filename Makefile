all: miditones miditones_scroll

miditones: miditones.c
	gcc -O2 -Wall -o $@ $<

miditones_scroll: miditones_scroll.c
	gcc -O2 -Wall -o $@ $<

clean:
	rm -f miditones miditones_scroll
