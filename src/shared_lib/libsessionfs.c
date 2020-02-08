#include "libsessionfs.h"

// we define the function pointers to the open and the close
orig_open_type orig_open;
orig_close_type orig_glose;

///a program constructor which saves the original value for the `open` and `close` symbols.
static __attribute__((constructor)) void init_method(){
	orig_read = (orig_open_type) dlsym(RTLD_NEXT, "read");
	orig_close = (orig_open_type) dlsym(RTLD_NEXT,"close");
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
			printf("calling kernel module")
		#endif
		return 0; /// \todo replace with an actual ioctl call
	} else {
		return orig_open(pathname, flags);
	}
}

/**
 * 
 */
int close(int filedes){
	
}
