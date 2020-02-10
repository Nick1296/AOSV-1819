#define O_SESS 00000004

#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif

int __real_open(const char* pathname,int flags);
int __real_close(int filedes);

int __wrap_open(const char* pathname, int flags){
	int flag=0;
	flag=flags & O_SESS;
	if(flag==4){
#ifdef DEBUG
			printf("calling kernel module\n");
#endif
		return 0; /// \todo replace with an actual ioctl call
	} else {
#ifdef DEBUG
		printf("calling libc open\n");
#endif
		return __real_open(pathname, flags);
	}
}

/**
 * \todo document and wrap close
 */
int __wrap_close(int filedes){
#ifdef DEBUG
	printf("calling libc close\n");
#endif
	return __real_close(filedes);
}
