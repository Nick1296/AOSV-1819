#enables warnings
CCOPTS= -Wall -Wstrict-prototypes
#enables gdb debug options
CCOPTS-DBG= -ggdb
#shard library options to link it
LIB_PATH= -L$(shell pwd)/../shared_lib
LIB= -lsessionfs
#enables ASAN
TEST-OPT= -fsanitize=address
CC= gcc

BINS= demo

OBJS = userspace_test.c

.phony: clean all demo demo-test


all: demo-lib

#compile the userspace program
demo-lib: $(OBJS)
		$(CC) $(LIB_PATH) $(CCOPTS) -o demo $(OBJS) $(LIB)

#compile the userspace program with ASAN and gdb options
demo-lib-d:
		$(CC) $(LIB_PATH) $(CCOPTS) $(CCOPTS-DBG) -o demo $(OBJS) $(LIB) $(TEST-OPT)

clean:
	rm -rf *.o *~  $(BINS)
