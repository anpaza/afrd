all:	cfg_parse.c main.c cfg_parse.h
	gcc -Wall -Wextra -ansi -pedantic -O2 -pipe -fomit-frame-pointer -march=native -c cfg_parse.c
	gcc -Wall                         -O2 -pipe -fomit-frame-pointer -march=native -c main.c
	gcc -Wall -Wextra -ansi -pedantic -O2 -pipe -fomit-frame-pointer -march=native -o test *.o

clean:
	rm -f test *.o config_new.ini
