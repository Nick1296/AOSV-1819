
/// Enables RTLD_NEXT macro.
#define _GNU_SOURCE
// dlsym function
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>


//to enable PATH_MAX
#include<errno.h>
#include <limits.h>

///For the definition of the `O_SES` flag.
#include "libsessionfs.h"

///For the ioctls methods exposed by our char device
#include "../kmodule/device_sessionfs.h"

///The path of our device file
#define DEV_PATH "/dev/SessionFS_dev"

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
int close(int __fd){
	int res;
	//we prepare a sess_params struct to remove the incarnation
	struct sess_params* params=malloc(sizeof(struct sess_params));
	memset(params,0,sizeof(struct sess_params));
	if(params==NULL){
		return -ENOMEM;
	}
	char* inc_path=malloc(sizeof(char)*PATH_MAX);
	memset(inc_path,0,sizeof(char)*PATH_MAX);
	if(inc_path==NULL){
		return -ENOMEM;
	}
	params->orig_path=NULL;
	params->filedes=__fd;
	params->pid=getpid();
	params->inc_path=inc_path;
	memset(inc_path,0,PATH_MAX);
	//we open the device
	int dev;
	dev=orig_open(DEV_PATH,O_WRONLY);
	if(dev<0){
		return dev;
	}
	printf("calling kernel module to remove the session\n");
	//we remove the incarnation
	res=ioctl(dev,IOCTL_SEQ_CLOSE,params);
	if(res<0){
		free(inc_path);
		free(params);
		return res;
	}
	printf("calling libc close to remove the file descriptor\n");
	//we call libc close
	res=orig_close(__fd);
	if(res<0){
		free(inc_path);
		free(params);
		return res;
	}
	printf("calling removing the incarnation file\n");
	//we delete the incarnation
	res=remove(inc_path);
	free(inc_path);
	free(params);
	return res;
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
int open(const char* __file, int __oflag, ...){
	int flag=0, res=0,dev;
	//we get the session path from the device
	char *sess_path=malloc(sizeof(char)*PATH_MAX),*path=NULL;
	printf("reading the current session path\n");
	res=get_sess_path(sess_path,PATH_MAX);
	if(res<0){
		return res;
	}
	printf("current session path:%s \n given pathname:%s\n", sess_path,__file);
	//we check if the file is in the right path
	path=strstr(__file,sess_path);
	free(sess_path);
	// check for the presence of the O_SESS flag
	flag=__oflag & O_SESS;
	if(flag==4 && path!=NULL){
		printf("detected O_SESS flag and correct path\n");
		//we open the device
		dev=orig_open(DEV_PATH,O_WRONLY);
		if(dev<0){
			return dev;
		}
		//we prepare an instance of the sess_params struct
		struct sess_params* params=malloc(sizeof(struct sess_params));
		memset(params,0,sizeof(struct sess_params));
		if(params==NULL){
			return -1;
			/// \todo set errno to ENOMEM
		}
		params->orig_path=__file;
		params->flags=__oflag;
		params->pid=getpid();
		params->inc_path=NULL;
		printf("calling kernel module to create a new incarnation\n");
		res=ioctl(dev,IOCTL_SEQ_OPEN,params);
		if(res<0){
			free(params);
			return res;
		}
		//we check if the created session is valid
		if(params->valid < VALID_SESS){
			printf("session invalid: closing\n");
			//if is invalid we need to call our close
			close(params->filedes);
		}
		res=params->filedes;
		free(params);
		return res;
	} else {
		printf("calling libc open\n");
		return orig_open(__file, __oflag);
	}
}

/**
 * Reads from the device located at ::DEV_PATH.
 */
int get_sess_path(char* buf,int bufsize){
	int dev=-1,res=0;
	dev=orig_open(DEV_PATH, O_RDONLY);
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
	dev=orig_open(DEV_PATH,O_WRONLY);
	if(dev<0){
		return dev;
	}
	res=write(dev,path,pathlen);
	return res;
}