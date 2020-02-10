#include "libsessionfs.h"

#define DEBUG

///we use printf to debug the wrapping
#ifdef DEBUG
#include <stdio.h>
#endif

///we use a typedef to alias the function pointer to the libc `open`.
typedef int (*orig_open_type)(const char* pathname, int flags);

/// we use a typedef to alis the function pointer to the libc `close`.
typedef int (*orig_close_type)(int filedes);

// we define the function pointers to the open and the close
orig_open_type orig_open;
orig_close_type orig_close;

///a program constructor which saves the original value for the `open` and `close` symbols.
static __attribute__((constructor)) void init_method(void){
	orig_open = (orig_open_type) dlsym(RTLD_NEXT, "read");
	orig_close = (orig_close_type) dlsym(RTLD_NEXT,"close");
}

/**
 * This function will check for the presence of the ::O_SESS flag, if this flag is present, the function will perform an ioctl call to the SessionFS kernel module, to open a new session for the given pathname.
 * If the ::O_SESS flag is not present, the function will call the libc implementation of the `open` systemcall.
 */
int open(const char* pathname, int flags){
	int flag=0;
	flag=flags & O_SESS;
	if(flag==4){
#ifdef DEBUG
			printf("calling kernel module\n");
#endif
		return 0; /// \todo replace with an actual ioctl call
	} else {
		printf("calling libc open\n");
		return orig_open(pathname, flags);
	}
}

/**
 *
 */
int close(int filedes){
#ifdef DEBUG
	printf("calling libc close\n");
#endif
	return orig_close(filedes);
}
