CFLAGS = -Wall -Werror

goatmalloc.o: goatmalloc.c goatmalloc.h
	gcc -c goatmalloc.c $(CFLAGS)

test_goatmalloc.o: test_goatmalloc.c goatmalloc.h
	gcc -c test_goatmalloc.c

test_goatmalloc: test_goatmalloc.o goatmalloc.o
	gcc -o test_goatmalloc test_goatmalloc.o goatmalloc.o

clean:
	rm -f goatmalloc.o test_goatmalloc.o test_goatmalloc

all: goatmalloc.o test_goatmalloc.o test_goatmalloc