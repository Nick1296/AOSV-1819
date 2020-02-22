CCOPTS= -Wall -Wstrict-prototypes
LIBS= -L $(PWD)/shared_lib/libsessionfs.so
CC= gcc
AR= ar


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
		$(CC) $(CCOPTS) -o demo $(OBJS) $(LIBS)

clean:
	rm -rf *.o *~  $(BINS)
