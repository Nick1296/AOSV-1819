CCOPTS=-shared -fPIC -Wall -Wstrict-prototypes
LIBS=-ldl
CC=gcc
AR=ar


BINS= libsessionfs

OBJS = libsessionfs.c

HEADERS=libsessionfs.h

.phony: clean all libsessionfs libsessionfs-test


all:	$(BINS)

libsessionfs: $(OBJS)
		$(CC) $(CCOPTS)  -o libsessionfs.so $(OBJS) $(LIBS)

clean:
	rm -rf *.so *.o *~  $(BINS)
