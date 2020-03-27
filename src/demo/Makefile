CCOPTS= -Wall -Wstrict-prototypes -ggdb
LIB_PATH= -L$(shell pwd)/../shared_lib
LIB= -lsessionfs -fsanitize=address
CC= gcc


BINS= demo

OBJS = userspace_test.c

HEADERS=

.phony: clean all demo demo-test


all: demo-lib

demo.o: $(OBJS)
		$(CC) -c -ggdb -o demo.o $(OBJS) -fsanitize=address

wrap.o: wrap.c
		$(CC) -c -ggdb -o wrap.o wrap.c -fsanitize=address

demo-wrap: wrap.o demo.o
		$(CC) -Wl,--wrap=open -Wl,-wrap=close demo.o wrap.o -o demo

demo-lib: $(OBJS)
		$(CC) $(LIB_PATH) $(CCOPTS) -o demo $(OBJS) $(LIB)

clean:
	rm -rf *.o *~  $(BINS)
