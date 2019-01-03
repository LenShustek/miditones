miditones: miditones.c
	gcc -O2 -Wall -o miditones miditones.c 

miditones_scroll: miditones_scroll.c
	gcc -O2 -Wall -o $@ $<

clean:
	rm -f miditones
