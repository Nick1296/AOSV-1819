# enables warnings, Position Independent Code
CCOPTS= -shared -fPIC -Wall -Werror -Wstrict-prototypes
#enables gdb debug options
CCOPTS-DBG= -ggdb
LIBS=-ldl
# enables ASAN
TEST-OPT= -fsanitize=address
CC=gcc


BINS= libsessionfs

OBJS = libsessionfs.c

HEADERS=libsessionfs.h

.phony: clean all libsessionfs libsessionfs-test


all:	$(BINS)

libsessionfs: $(OBJS)
		$(CC) $(CCOPTS)  -o libsessionfs.so $(OBJS) $(LIBS)

libsessionfs-d: $(OBJS)
		$(CC) $(CCOPTS) $(CCOPTS-DBG)  -o libsessionfs.so $(OBJS) $(LIBS) $(TEST-OPT)

clean:
	rm -rf *.so *.o *~  $(BINS)
