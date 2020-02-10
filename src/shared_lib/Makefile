CCDBGOPTS=-shared -fPIC -Wall -ggdb -Wstrict-prototypes -fsanitize=address
CCOPTS=-shared -fPIC -Wall -Wstrict-prototypes
LIBS=-ldl
CC=gcc
AR=ar


BINS= libsessionfs

OBJS = libsessionfs.c

HEADERS=libsessionfs.h

.phony: clean all libsessionfs libsessionfs-test


all:	$(BINS)

libsessionfs-test: $(OBJS)
		$(CC) $(CCDBGOPTS)  -o libsessionfs.so $(OBJS) $(LIBS)

libsessionfs: $(OBJS)
		$(CC) $(CCOPTS)  -o libsessionfs.so $(OBJS) $(LIBS)

clean:
	rm -rf *.o *~  $(BINS)
