#define DEBUG

/// Enables RTLD_NEXT macro.
#define _GNU_SOURCE
// dlsym function
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>

///For the definition of the `O_SES` flag.
#include "libsessionfs.h"

///For the ioctls methods exposed by our char device
#include "../kmodule/device_sessionfs.h"

///The path of our device file
#define DEV_PATH "/dev/SessionFS_dev"

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
 * \brief Wraps the close determining if it must call the libc `close` or the SessionFS module.
 * \param[in] filedes file descriptor to deallocate, same as libc `open`'s `fildes`.
 * \returns 0 on success, -1 on error, setting errno to indicate the error value.
 * \todo see if is beeter to locate the incarnation filename from the library (by reading from `/proc/self/fd/filedes`)
 */
int close(int filedes){
	//we prepare a sess_params struct to remove the incarnation
		struct sess_params* params=calloc(sizeof(sess_params));
		if(params==NULL){
			return -ENOMEM;
		}
		params->orig_path=NULL;
		params->fildes=filedes;
		params->pid=getpid();
		params->inc_path=calloc(sizeof(char)*PATH_MAX);
		if(params->inc_path==NULL){
			free(params);
			return -ENOMEM;
		}
		//we remove the incarnation
		res=ioctl(dev,IOCTL_SEQ_CLOSE,sess_params);
		if(res<0){
			free(params->inc_path);
			free(params);
			return res;
		}
		//we call libc close
		res=close(filedes);
		if(res<0){
			free(params->inc_path);
			free(params);
			return res;
		}
		//we delete the incarnation
		res=remove(params);
		free(params->inc_path);
		free(params);
		return res;

	return orig_close(filedes);
}

/**
 * \brief Wraps the open determining if it must call the libc `open` or the SessionFS module.
 * \param[in] pathname The pathname of the file to be opened, same usage an type of the libc `open`'s `pathname`.
 * \param[in] flags flags to determine the file status flag and the access modes, same as the libc `open`'s `oflag`, however a possible flag is the ::O_SESS flag which enables the session semantic.
 * \returns It will return a file descriptor if the operation is successful, both for the libc version and for the module return value.
 *
 * This function will check for the presence of the ::O_SESS flag, if this flag is present, the function will perform an ioctl call to the SessionFS kernel module, to open a new session for the given pathname.
 * If the ::O_SESS flag is not present, the function will call the libc implementation of the `open` systemcall.
 */
int open(const char* pathname, int flags){
	int flag=0, res=0,dev=-1;
	//we get the session path from the device
	char *sess_path=malloc(sizeof(char)*PATH_MAX),path=NULL;
	res=get_sess_path(sess_path,PATH_MAX);
	if(res<0){
		return res;
	}
	//we check if the file is in the right path
	path=strstr(pathname,sess_path);
	free(sess_path);
	// check for the presence of the O_SESS flag
	flag=flags & O_SESS;
	if(flag==4 && path!=NULL){
#ifdef DEBUG
			printf("calling kernel module\n");
#endif
		//we open the device
		dev=open(DEV_PATH,O_WRONLY);
		if(dev<0){
			return dev;
		}
		//we prepare an instance of the sess_params struct
		struct sess_params* params=calloc(sizeof(sess_params));
		if(params==NULL){
			return -ENOMEM;
		}
		params->orig_path=pathname;
		params->flags=flags;
		params->pid=getpid();
		params->inc_path=NULL;
		res=ioctl(dev,IOCTL_SEQ_OPEN,sess_params);
		if(res<0){
			free(params);
			return res;
		}
		//we check if the created session is valid
		if(params->valid < VALID_SESS){
			//if is invalid we need to call our close
			close(params->filedes);
		}
		res=params->filedes;
		free(sess_params);
		return res;
	} else {
#ifdef DEBUG
		printf("calling libc open\n");
#endif
		return orig_open(pathname, flags);
	}
}

/**
 * Reads from the device located at ::DEV_PATH.
 */
int get_sess_path(char* buf,bufsize){
	int dev=-1,res=0;
	dev=open(DEV_PATH, O_RDONLY);
	if(dev<0){
		return dev;
	}
	res=read(dev,buf,bufsize);
	return res;
}

/** Writes on the device at ::DEV_PATH.
 */
int write_sess_path(char* path,int pathlen){
	int dev=-1, res=0;
	dev=open(DEV_PATH,O_WRONLY);
	if(dev<0){
		return dev;
	}
	res=write(dev,path,pathlen);
	return res;
}