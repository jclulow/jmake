

CC =	gcc


jmake:	jmake.c ents.c parser.c fileread.c util.c
	$(CC) -o $@ $^
