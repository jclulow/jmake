

CC =	gcc


jmake:	jmake.c ents.c reader.c
	$(CC) -o $@ $^
