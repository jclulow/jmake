

CC =	gcc


jmake:	jmake.c
	$(CC) -o $@ $^
