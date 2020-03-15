CCOPTS= -Wall -Wstrict-prototypes
LIB_PATH= -L$(shell pwd)/../shared_lib
LIB= -lsessionfs
CC= gcc


BINS= demo

OBJS = userspace_test.c

HEADERS=

.phony: clean all demo demo-test


all: demo-lib

demo.o: $(OBJS)
		$(CC) -c -o demo.o $(OBJS)

wrap.o: wrap.c
		$(CC) -c -o wrap.o wrap.c

demo-wrap: wrap.o demo.o
		$(CC) -Wl,-wrap=open -Wl,-wrap=close demo.o wrap.o -o demo

demo-lib: $(OBJS)
		$(CC) $(LIB_PATH) $(CCOPTS) -o demo $(OBJS) $(LIB)

clean:
	rm -rf *.o *~  $(BINS)
