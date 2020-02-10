CCDBGOPTS= -Wall -ggdb -Wstrict-prototypes -fsanitize=address
CCOPTS= -Wall -Wstrict-prototypes
LIBS= -L $(PWD)/shared_lib/libsessionfs.so
CC= gcc
AR= ar


BINS= demo

OBJS = userspace_test.c

HEADERS=

.phony: clean all demo demo-test


all: demo-prog

demo-test: $(OBJS)
		$(CC) $(CCDBGOPTS)  -o demo $(OBJS) $(LIBS)

demo-prog: $(OBJS)
		$(CC) $(CCOPTS)  -o demo $(OBJS) $(LIBS)

clean:
	rm -rf *.o *~  $(BINS)
