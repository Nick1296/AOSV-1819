CCOPTS= -shared -fPIC -Wall -Werror -Wstrict-prototypes
LIBS=-ldl
CC=gcc


BINS= libsessionfs

OBJS = libsessionfs.c

HEADERS=libsessionfs.h

.phony: clean all libsessionfs libsessionfs-test


all:	$(BINS)

libsessionfs: $(OBJS)
		$(CC) $(CCOPTS)  -o libsessionfs.so $(OBJS) $(LIBS)

clean:
	rm -rf *.so *.o *~  $(BINS)
